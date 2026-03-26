#include <cassert>
#include <cmath>
#include <random>
#include <iostream>
#include <ranges>
namespace views = std::views;
#include "schedulers/ext/ext.hpp"

#ifdef SIM_MODEL_PATH
    #define STRINGIFY(X) STRINGIFY2(X)
    #define STRINGIFY2(X) #X
    #include STRINGIFY(SIM_MODEL_PATH)
#else
    #include "sim-model.hpp"
#endif

#define meta 


std::mt19937 gen(std::random_device{}());

double random(double max) {
    std::uniform_real_distribution<> d(0, max);
    return d(gen);
}


/*** MODEL ***/
constexpr int NUM_PHASES = ROSSA_NUM_PHASES;
constexpr int NUM_NODES = ROSSA_NUM_NODES;
constexpr int NUM_FLOWS = ROSSA_NUM_FLOWS;
constexpr int MAX_FLOW_TIME = ROSSA_MAX_FLOW_TIME;
constexpr int NUM_SWITCHES = ROSSA_NUM_SWITCHES;
constexpr int NUM_PORTS = NUM_NODES * NUM_SWITCHES;
constexpr int BUFFER_SIZE = NUM_NODES * NUM_FLOWS;
constexpr int SCHEDULE_SIZE = NUM_NODES * NUM_FLOWS * (NUM_SWITCHES + 1);

typedef struct {
    node_t ingress;
    node_t egress;
    packet_t amount_over_time[MAX_FLOW_TIME];
} flow_with_amounts_t;

const packet_t NODE_CAPACITIES[NUM_NODES] = ROSSA_GEN_NODE_CAPACITIES;

const packet_t PORT_BANDWIDTHS[NUM_PORTS] = ROSSA_GEN_PORT_BANDWIDTHS;

const node_t TOPOLOGY[NUM_PHASES][NUM_PORTS] = ROSSA_GEN_TOPOLOGY;

const flow_with_amounts_t FLOWS[NUM_FLOWS] = ROSSA_GEN_FLOWS;

/*** Convenience functions/macros ***/
#define loop(num, i, ...) for (const auto i : views::iota(0, num)) { __VA_ARGS__ }
#define for_phases(i, ...) loop(NUM_PHASES, i, __VA_ARGS__)
#define for_nodes(i, ...) loop(NUM_NODES, i, __VA_ARGS__)
#define for_switches(i, ...) loop(NUM_SWITCHES, i, __VA_ARGS__)
#define for_ports(i, ...) loop(NUM_PORTS, i, __VA_ARGS__)
#define for_flows(i, ...) loop(NUM_FLOWS, i, __VA_ARGS__)

constexpr port_t port_of(node_t node, switch_t sw) { return node * NUM_SWITCHES + sw; }
constexpr node_t port_owner(port_t port) { return port / NUM_SWITCHES; }

/*** STATE ***/
bool gDidOverflow = false; // Whether any port at any time overflowed
int gCurrentPhase = 0; // Current phase of the system (will cycle).
int gCurrentStep = 0; // Non-cyclic phase step counter.
int gCurrentFlowStep = 0;
packet_t gNodeBuffers[BUFFER_SIZE]{};
packet_t gPortSent[NUM_PORTS]{};
meta packet_t maxSendFromPortInPhase = 0;

// Sampling
constexpr bool sampling = true;
int sampleIntroIndex[NUM_FLOWS]; // The sampled packets place in queue outside network (num packets before it)
int sampleEntryStep[NUM_FLOWS];
int32_t sampleNodePosition[NUM_FLOWS]; // The position of the packet in the flow (num packets before it)
node_t sampleNode[NUM_FLOWS]; // The node it is in.
int sampleLatency[NUM_FLOWS]; // The result latency.


packet_t get_buffer(node_t node, flow_t flow) {
    return gNodeBuffers[node * NUM_FLOWS + flow];
}
void set_buffer(node_t node, flow_t flow, packet_t value) {
    gNodeBuffers[node * NUM_FLOWS + flow] = value;
}

void ON_CONSTRUCT() {
    packet_t nodeData[NUM_NODES] = ROSSA_GEN_NODE_CAPACITIES;
    packet_t portData[NUM_PORTS] = ROSSA_GEN_PORT_BANDWIDTHS;
    node_t topoData[NUM_PORTS];

    // Copy network parameters and content to scheduler
    for_nodes(node, nodeData[node] = NODE_CAPACITIES[node];)
    for_ports(port, portData[port] = PORT_BANDWIDTHS[port];)
    extPushNetwork(NUM_PHASES, NUM_NODES, NUM_FLOWS, NUM_SWITCHES, nodeData, portData);
    for_flows(flow, extPushFlow(flow, FLOWS[flow].ingress, FLOWS[flow].egress);)
    // Topology is 2d-array, so copy piece by piece
    for_phases(phase,
        for_ports(port,
            topoData[port] = TOPOLOGY[phase][port];
        )
        extPushTopology(phase, topoData);
    )
    extSchedulerInit();
}

void ON_BEGIN() {
    // Initialize local data
    gDidOverflow = false;
    gCurrentPhase = 0;
    gCurrentStep = 0;
    for_nodes(node,
        for_flows(flow,
            set_buffer(node, flow, 0);
        )
    )
    for_ports(port, gPortSent[port] = 0;)
    maxSendFromPortInPhase = 0;

    // Let the scheduler initialize itself
    extSchedulerInit();
}


/*** CONSTRAINTS ***/

bool verifyTopology() {
    // Check no self-flows
    for_flows(flow,
        if (FLOWS[flow].ingress == FLOWS[flow].egress) {
            return false;
        }
    )
    return true;
}
bool verifyConstraints() {
    return verifyTopology();
}

/*** TRANSITION ***/

void setup() {
    if (sampling) {
        // Hardcoded assume 50 steps for good enough stabilisation
        constexpr double fStepsToStable = 50.0;
        for_flows(flow, 
            sampleLatency[flow] = -1;
            sampleEntryStep[flow] = -1;
            sampleNode[flow] = -1;
            sampleNodePosition[flow] = -1;
            sampleIntroIndex[flow] = static_cast<int>(trunc(fStepsToStable + random(fStepsToStable)));
        )
    }
}

/** SAMPLING **/

double maxSampleLatency() {
    double max = 0.0;
    for_flows(f,
        max = fmax(max, sampleLatency[f]);
    )
    return max;
}
double averageSampleLatency() {
    // double total = sum(f : flows_t) sampleLatency[f];
    double total = std::accumulate(std::begin(sampleLatency), std::end(sampleLatency), 0.0);
    return total / NUM_FLOWS;
}

void sampleIngressAdded(flow_t flow, packet_t amount) {
    // If the sampling packet has not entered the network yet.
    if (sampleIntroIndex[flow] >= 0) {
        sampleIntroIndex[flow]--;
        if (sampleIntroIndex[flow] < 0) {
            // Enter the network.
            const node_t node = FLOWS[flow].ingress;
            // We already just did this as part of normal scheduling:
            //    get_buffer(node, flow) += amount;
            // So add our now non-positive ingress position to the amount stored.
            // E.g. if index is now -1 then we were the last to make it. -6 we are the 6th last etc.
            sampleNodePosition[flow] = get_buffer(node, flow) + static_cast<packet_t>(trunc(random(amount)));
            sampleEntryStep[flow] = gCurrentStep;
            sampleNode[flow] = node;
        }
    }
}

void samplePortTransfer(flow_t flow, node_t destNode, packet_t amountSendNode, packet_t amountDestNode) {
    if (sampleLatency[flow] == -1 && sampleNodePosition[flow] >= 0) {
        // We have not sampled the final latency value yet and have been injected.
        sampleNodePosition[flow] -= amountSendNode;
        if (sampleNodePosition[flow] < 0) {
            // The packet leaves the port.
            if (destNode == FLOWS[flow].egress) {
                // Packet leaves network
                sampleLatency[flow] = gCurrentStep - sampleEntryStep[flow];
                sampleNodePosition[flow] = -1;
                sampleNode[flow] = -1;
            } else {
                // Goes to another port
                // Proportionally re-calculate position among packets arriving at destination (from back with negative position).
                sampleNodePosition[flow] *= static_cast<double>(amountDestNode) / static_cast<double>(amountSendNode);
                // Our new position is how much there is now "plus our negative position" since we subtracted above.
                // 0-index. If 10 packets was added, and were -10 then we are the first, if -9 we are the last.
                sampleNodePosition[flow] += get_buffer(destNode, flow);
                sampleNode[flow] = destNode;
            }
        }
    }
}


/** NORMAL UPDATE **/

double portUtilization(port_t p) {
    double dSent = gPortSent[p];
    return dSent / PORT_BANDWIDTHS[p];
}

packet_t packetsAtNode(node_t node) {
    packet_t sum = 0;
    for_flows(flow, 
        sum += get_buffer(node, flow);
    )
    return sum;
}

packet_t totalPacketsBuffered() {
    packet_t sum = 0;
    for_nodes(node, sum += packetsAtNode(node);)
    return sum;
}

void nextPhase() {
    gCurrentPhase += 1;
    if (gCurrentPhase == NUM_PHASES) {
        gCurrentPhase = 0;
    }
    gCurrentFlowStep += 1;
    if (gCurrentFlowStep == MAX_FLOW_TIME) {
        gCurrentFlowStep = 0;
    }
    if (sampling) {
        gCurrentStep += 1;
    }
}

bool validNodeState(node_t n) {
    return packetsAtNode(n) <= NODE_CAPACITIES[n];
}

bool updateValidState() {
    for_nodes(node, 
        if (!validNodeState(node)) {
            gDidOverflow = true;
            return false;
        }
    )
    return true;
}

void simulatePhase() {
    // The current sending phase.
    const phase_t phase = gCurrentPhase;
    packet_t sentPort[NUM_PORTS][NUM_FLOWS]{};
    packet_t recv[NUM_NODES][NUM_FLOWS]{};
    packet_t sentNode[NUM_NODES][NUM_FLOWS]{};
    double schedule[NUM_FLOWS][NUM_SWITCHES];

    packet_t schedule_choice_output[SCHEDULE_SIZE];
    extGetScheduleChoiceAll(phase, gNodeBuffers, schedule_choice_output);

    // Calculate sent (only relevant for current phase 'i')
    for_nodes(node, 
        for_flows(flow,
            for_switches(sw, 
                schedule[flow][sw] = 0;
            )
        )
        for_flows(flow,
            packet_t buffered = get_buffer(node, flow);
            packet_t weights[NUM_SWITCHES];
            packet_t sum = schedule_choice_output[(node * NUM_FLOWS + flow) * (NUM_SWITCHES + 1)];  // a dummy switch to allow not attempting to send all buffered packets in the flow.
            for_switches(sw, 
                packet_t choice_weight = schedule_choice_output[(node * NUM_FLOWS + flow) * (NUM_SWITCHES + 1) + sw + 1];
                weights[sw] = choice_weight;
                sum += choice_weight;
            )
            for_switches(sw, 
                schedule[flow][sw] = sum == 0 ? 0 : buffered * (weights[sw] / static_cast<double>(sum));
            )
        )

        for_switches(sw,
            port_t p = port_of(node, sw);
            packet_t portSending = 0;

            // If port is a self-loop in the current phase, just keep the packets. (This is to avoid issues with latency sampling).
            if (TOPOLOGY[phase][p] != node) {
                double sum = 0;
                for_flows(flow,
                    sum += schedule[flow][sw];
                )
                double flow_rate = sum > 0.0 ? fmin(1.0, PORT_BANDWIDTHS[p] / sum) : 0.0;

                for_flows(flow,
                    const auto sending = static_cast<packet_t>(trunc(schedule[flow][sw] * flow_rate));
                    portSending += sending;
                    sentPort[p][flow] = sending;
                )
                // If we send less that bandwidth due to rounding down to integer, add packets to the flows with the largest rounding errors.
                if (flow_rate != 1) while (portSending < PORT_BANDWIDTHS[p]) {
                    double max_diff = 0;
                    flow_t flow_with_max_diff = -1;
                    for_flows(flow,
                        double diff = schedule[flow][sw] * flow_rate - sentPort[p][flow];
                        if (sentPort[p][flow] < schedule[flow][sw] && diff > max_diff) {
                            max_diff = diff;
                            flow_with_max_diff = flow;
                        }
                    )
                    if (flow_with_max_diff == -1) break;  // If no flows can add packets, stop.
                    portSending++;
                    sentPort[p][flow_with_max_diff]++;
                }
            }
            gPortSent[p] = portSending;
            maxSendFromPortInPhase = portSending > maxSendFromPortInPhase ? portSending : maxSendFromPortInPhase;
        )
        for_flows(flow,
            for_switches(sw,
                sentNode[node][flow] += sentPort[port_of(node, sw)][flow];
            )
        )
    )
    // Calculate received
    for_flows(flow, // For all flows
        for_ports(pSender,  // For any possible port sender
            const node_t destNode = TOPOLOGY[phase][pSender];  // Which node is the receiver
            if (destNode != FLOWS[flow].egress) {  // Egress means packets leave.
                //Add the sent packages to the receiving node.
                recv[destNode][flow] += sentPort[pSender][flow];
            }
        )
    )
    // Update with send/recv
    for_nodes(node, 
        for_flows(flow,
            const packet_t toAdd = recv[node][flow] - sentNode[node][flow];
            set_buffer(node, flow, get_buffer(node, flow) + toAdd);
        )
    )

    if (sampling) {
        // Must be here after buffers are modified, but before new ingress.
        for_flows(flow, 
            if (sampleNode[flow] != -1 &&  // Sample for this flow did not yet ingress network.
                sentNode[sampleNode[flow]][flow] != 0) {  // Nothing sent on this flow.
                // Weighted sampling (if flow is split here).
                node_t node = sampleNode[flow];
                double sampledWeight = random(sentNode[node][flow]);
                packet_t sum = 0;
                port_t sampledPort = -1;
                for_switches(sw,
                    port_t port = port_of(node, sw);
                    const auto w = sentPort[port][flow];
                    if (sampledWeight >= sum && sampledWeight < sum + w) sampledPort = port;
                    sum += w;
                )
                assert(sampledPort >= 0);
                node_t destNode = TOPOLOGY[phase][sampledPort];
                samplePortTransfer(flow, destNode, sentNode[node][flow], recv[destNode][flow]);
            }
        )
    }

    // Add ingress
    for_flows(flow, 
        const node_t node = FLOWS[flow].ingress;
        packet_t amount = FLOWS[flow].amount_over_time[gCurrentFlowStep];
        set_buffer(node, flow, get_buffer(node, flow) + amount);
        if (sampling) {
            sampleIngressAdded(flow, amount);
        }
    )
    updateValidState();
    nextPhase();
}

void print_node_and_port_header() {
    std::cout << "step; gDidOverflow; ";
    for_nodes(node, std::cout << "packetsAtNode(" << node << "); ";)
    for_ports(port, std::cout << "portUtilization(" << port << "); ";)
    std::cout << "\n";
}

void print_node_and_port(int step) {
    std::cout << step << "; " << std::boolalpha << gDidOverflow << "; ";
    for_nodes(node, std::cout << packetsAtNode(node) << "; ";)
    for_ports(port, std::cout << portUtilization(port) << "; ";)
    std::cout << "\n";
}

void print_sampling_header() {
    std::cout << "sample_id; ";
    for_flows(flow,
        std::cout << "sampleLatency[" << flow << "]; ";
    )
    std::cout << "\n";
}

void print_sampling_line(int sample_id) {
    std::cout << sample_id << "; ";
    for_flows(flow,
        std::cout << sampleLatency[flow] << "; ";
    )
    std::cout << "\n";
}

template<typename OutFn>
void run_one_simulation(int steps, OutFn&& output) {
    ON_BEGIN();
    assert(verifyConstraints());
    setup();
    int t = 0;
    output(t);
    while (t < steps) {
        t++;
        simulatePhase();
        output(t);
    }
}

int main() {
    ON_CONSTRUCT();
    print_node_and_port_header();
    run_one_simulation(ROSSA_SIM_STEPS, print_node_and_port);

    std::cout << "@@@\n";  // Print seperator

    print_sampling_header();
    for (int sample_id = 0; sample_id < ROSSA_SAMPLING_COUNT; ++sample_id) {
        run_one_simulation(ROSSA_SAMPLING_STEPS, [](int step){});
        print_sampling_line(sample_id);
    }
}
