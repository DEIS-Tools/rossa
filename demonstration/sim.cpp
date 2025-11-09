#include <cassert>
#include <cmath>
#include <random>
#include <iostream>
#include <ranges>
namespace views = std::views;
#include "schedulers/ext/ext.hpp"
#include "traffic_flow/ext/traffic.hpp"

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
constexpr int MAX_FLOWS = NUM_NODES * NUM_NODES;
constexpr int NUM_SWITCHES = ROSSA_NUM_SWITCHES;
constexpr int NUM_PORTS = NUM_NODES * NUM_SWITCHES;

const packet_t NODE_CAPACITIES[NUM_NODES] = ROSSA_GEN_NODE_CAPACITIES;

const packet_t PORT_BANDWIDTHS[NUM_PORTS] = ROSSA_GEN_PORT_BANDWIDTHS;

const node_t TOPOLOGY[NUM_PHASES][NUM_PORTS] = ROSSA_GEN_TOPOLOGY;

/*** Convenience functions/macros ***/
#define loop(num, i, ...) for (const auto i : views::iota(0, num)) { __VA_ARGS__ }
#define for_phases(i, ...) loop(NUM_PHASES, i, __VA_ARGS__)
#define for_nodes(i, ...) loop(NUM_NODES, i, __VA_ARGS__)
#define for_switches(i, ...) loop(NUM_SWITCHES, i, __VA_ARGS__)
#define for_ports(i, ...) loop(NUM_PORTS, i, __VA_ARGS__)

constexpr port_t port_of(node_t node, switch_t sw) { return node * NUM_SWITCHES + sw; }
constexpr node_t port_owner(port_t port) { return port / NUM_SWITCHES; }
/*** STATE ***/

// Filled out by traffic generator
int number_of_flows = 0;
node_t flow_ingress[MAX_FLOWS];
node_t flow_egress[MAX_FLOWS];

bool gDidOverflow = false; // Whether any port at any time overflowed
int gCurrentPhase = 0; // Current phase of the system (will cycle).
int gCurrentStep = 0; // Non-cyclic phase step counter.
packet_t gNodeBuffers[NUM_NODES][MAX_FLOWS]{};
packet_t gPortSent[NUM_PORTS]{};
meta packet_t maxSendFromPortInPhase = 0;

// Sampling
int sampleIntroIndex[MAX_FLOWS]; // The sampled packets place in queue outside network (num packets before it)
int sampleEntryStep[MAX_FLOWS];
int32_t sampleNodePosition[MAX_FLOWS]; // The position of the packet in the flow (num packets before it)
node_t sampleNode[MAX_FLOWS]; // The node it is in.
int sampleLatency[MAX_FLOWS]; // The result latency.


void ON_CONSTRUCT() {
    packet_t nodeData[NUM_NODES] = ROSSA_GEN_NODE_CAPACITIES;
    packet_t portData[NUM_PORTS] = ROSSA_GEN_PORT_BANDWIDTHS;
    node_t topoData[NUM_PORTS];

    // Initialize traffic flows
    trafficInit(NUM_NODES, number_of_flows, flow_ingress, flow_egress);

    // Copy network parameters and content to scheduler
    for_nodes(node, nodeData[node] = NODE_CAPACITIES[node];)
    for_ports(port, portData[port] = PORT_BANDWIDTHS[port];)
    extPushNetwork(NUM_PHASES, NUM_NODES, number_of_flows, NUM_SWITCHES, nodeData, portData, flow_ingress, flow_egress);
    // Topology is 2d-array, so copy piece by piece
    for_phases(phase,
        for_ports(port,
            topoData[port] = TOPOLOGY[phase][port];
        )
        extPushTopology(phase, topoData);
    )
}

void ON_BEGIN() {
    // Initialize local data
    gDidOverflow = false;
    gCurrentPhase = 0;
    gCurrentStep = 0;
    for_nodes(node,
        for (flow_t flow = 0; flow < MAX_FLOWS; ++flow) {
            gNodeBuffers[node][flow] = 0;
        }
    )
    for_ports(port, gPortSent[port] = 0;)
    maxSendFromPortInPhase = 0;

    // Let the scheduler initialize itself
    extSchedulerInit();
}

void pushBuffers() {
    for_nodes(node, extPushBuffers(node, gNodeBuffers[node]);)
}
void get_normalised_schedule(node_t node, flow_t flow, phase_t phase_i, double norm_schedule[NUM_SWITCHES]) {
    packet_t buffered = gNodeBuffers[node][flow];
    packet_t weights[NUM_SWITCHES];
    packet_t sum = extGetScheduleChoice(node, flow, phase_i, -1);  // -1 is a dummy switch to allow not attempting to send all buffered packets in the flow.
    for (const auto sw : views::iota(0, NUM_SWITCHES)) {
        const packet_t choice_weight = extGetScheduleChoice(node, flow, phase_i, sw);
        weights[sw] = choice_weight;
        sum += choice_weight;
    }
    for (const auto sw : views::iota(0, NUM_SWITCHES)) {
        norm_schedule[sw] = sum == 0 ? 0 : buffered * (weights[sw] / static_cast<double>(sum));
    }
}

/*** CONSTRAINTS ***/

bool verifyTopology() {
    // Check no self-flows
    for (const auto f : views::iota(0, number_of_flows)) {
        if (flow_ingress[f] == flow_egress[f]) {
            return false;
        }
    }
    return true;
}
bool verifyConstraints() {
    return verifyTopology();
}

/*** TRANSITION ***/

void setup() {
    // Hardcoded assume 50 steps for good enough stabilisation
    constexpr double fStepsToStable = 50.0;

    for (const auto f : views::iota(0, number_of_flows)) {
        sampleLatency[f] = -1;
        sampleEntryStep[f] = -1;
        sampleNode[f] = -1;
        sampleNodePosition[f] = -1;
        sampleIntroIndex[f] = static_cast<int>(trunc(fStepsToStable + random(fStepsToStable)));
    }
}

/** SAMPLING **/

double maxSampleLatency() {
    double max = 0.0;
    for (const auto f : views::iota(0, number_of_flows)) {
        max = fmax(max, sampleLatency[f]);
    }
    return max;
}
double averageSampleLatency() {
    // double total = sum(f : flows_t) sampleLatency[f];
    double total = std::accumulate(std::begin(sampleLatency), std::end(sampleLatency), 0.0);
    return total / number_of_flows;
}

void sampleIngressAdded(flow_t f, packet_t amount) {
    // If the sampling packet has not entered the network yet.
    if (sampleIntroIndex[f] >= 0) {
        sampleIntroIndex[f]--;
        if (sampleIntroIndex[f] < 0) {
            // Enter the network.
            const node_t n = flow_ingress[f];
            // We already just did this as part of normal scheduling:
            //    gNodeBuffers[n][f] += amount;
            // So add our now non-positive ingress position to the amount stored.
            // E.g. if index is now -1 then we were the last to make it. -6 we are the 6th last etc.
            sampleNodePosition[f] = gNodeBuffers[n][f] + static_cast<packet_t>(trunc(random(amount)));
            sampleEntryStep[f] = gCurrentStep;
            sampleNode[f] = n;
        }
    }
}

void samplePortTransfer(flow_t f, node_t destNode, packet_t amountSendNode, packet_t amountDestNode) {
    if (sampleLatency[f] == -1 && sampleNodePosition[f] >= 0) {
        // We have not sampled the final latency value yet and have been injected.
        sampleNodePosition[f] -= amountSendNode;
        if (sampleNodePosition[f] < 0) {
            // The packet leaves the port.
            if (destNode == flow_egress[f]) {
                // Packet leaves network
                sampleLatency[f] = gCurrentStep - sampleEntryStep[f];
                sampleNodePosition[f] = -1;
                sampleNode[f] = -1;
            } else {
                // Goes to another port
                // Proportionally re-calculate position among packets arriving at destination (from back with negative position).
                sampleNodePosition[f] *= static_cast<double>(amountDestNode) / static_cast<double>(amountSendNode);
                // Our new position is how much there is now "plus our negative position" since we subtracted above.
                // 0-index. If 10 packets was added, and were -10 then we are the first, if -9 we are the last.
                sampleNodePosition[f] += gNodeBuffers[destNode][f];
                sampleNode[f] = destNode;
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
    for (flow_t flow = 0; flow < number_of_flows; ++flow) {
        sum += gNodeBuffers[node][flow];
    }
    return sum;
}

packet_t totalPacketsBuffered() {
    packet_t sum = 0;
    for_nodes(node, sum += packetsAtNode(node);)
    return sum;
}

void nextPhase() {
    gCurrentStep += 1;
    gCurrentPhase += 1;
    if (gCurrentPhase == NUM_PHASES) {
        gCurrentPhase = 0;
    }
}

bool validNodeState(node_t n) {
    return packetsAtNode(n) <= NODE_CAPACITIES[n];
}

bool updateValidState() {
    for (const auto n : views::iota(0, NUM_NODES)) {
        if (!validNodeState(n)) {
            gDidOverflow = true;
            return false;
        }
    }
    return true;
}

void simulatePhase() {
    // The current sending phase.
    const phase_t phase = gCurrentPhase;
    packet_t sentPort[NUM_PORTS][MAX_FLOWS]{};
    packet_t recv[NUM_NODES][MAX_FLOWS]{};
    packet_t sentNode[NUM_NODES][MAX_FLOWS]{};

    // Push buffers and create schedule
    pushBuffers();
    extPrepareChoices();

    // Calculate sent (only relevant for current phase 'i')
    for (const auto node : views::iota(0, NUM_NODES)) {
        double schedule[MAX_FLOWS][NUM_SWITCHES]{};
        for (const auto flow : views::iota(0, number_of_flows)) {
            get_normalised_schedule(node, flow, phase, schedule[flow]);
        }
        for (const auto sw : views::iota(0, NUM_SWITCHES)) {
            port_t p = port_of(node, sw);
            packet_t portSending = 0;

            // If port is a self-loop in the current phase, just keep the packets. (This is to avoid issues with latency sampling).
            if (TOPOLOGY[phase][p] != node) {
                double sum = 0;
                for (const auto flow : views::iota(0, number_of_flows)) {
                    sum += schedule[flow][sw];
                }
                double flow_rate = sum > 0.0 ? fmin(1.0, PORT_BANDWIDTHS[p] / sum) : 0.0;

                for (const auto flow : views::iota(0, number_of_flows)) {
                    const auto sending = static_cast<packet_t>(trunc(schedule[flow][sw] * flow_rate));
                    portSending += sending;
                    sentPort[p][flow] = sending;
                }
                // If we send less that bandwidth due to rounding down to integer, add packets to the flows with the largest rounding errors.
                if (flow_rate != 1) while (portSending < PORT_BANDWIDTHS[p]) {
                    double max_diff = 0;
                    flow_t flow_with_max_diff = -1;
                    for (const auto flow : views::iota(0, number_of_flows)) {
                        double diff = schedule[flow][sw] * flow_rate - sentPort[p][flow];
                        if (sentPort[p][flow] < schedule[flow][sw] && diff > max_diff) {
                            max_diff = diff;
                            flow_with_max_diff = flow;
                        }
                    }
                    if (flow_with_max_diff == -1) break;  // If no flows can add packets, stop.
                    portSending++;
                    sentPort[p][flow_with_max_diff]++;
                }
            }
            gPortSent[p] = portSending;
            maxSendFromPortInPhase = portSending > maxSendFromPortInPhase ? portSending : maxSendFromPortInPhase;
        }
        for (const auto flow : views::iota(0, number_of_flows)) {
            for (const auto sw : views::iota(0, NUM_SWITCHES)) {
                sentNode[node][flow] += sentPort[port_of(node, sw)][flow];
            }
        }
    }
    // Calculate received
    for (const auto f : views::iota(0, number_of_flows)) { // For all flows
        for (const auto pSender : views::iota(0, NUM_PORTS)) { // For any possible port sender
            const node_t destNode = TOPOLOGY[phase][pSender]; // Which node is the receiver
            if (destNode != flow_egress[f]) { // Egress means packets leave.
                //Add the sent packages to the receiving node.
                recv[destNode][f] += sentPort[pSender][f];
            }
        }
    }
    // Update with send/recv
    for (const auto node : views::iota(0, NUM_NODES)) {
        for (const auto flow : views::iota(0, number_of_flows)) {
            packet_t toAdd = recv[node][flow] - sentNode[node][flow];
            gNodeBuffers[node][flow] += toAdd;
        }
    }

    // Must be here after buffers are modified, but before new ingress.
    for (const auto flow : views::iota(0, number_of_flows)) {
        node_t node = sampleNode[flow];
        if (node == -1) continue;  // Sample for this flow did not yet ingress network.
        if (sentNode[node][flow] == 0) continue;  // Nothing sent on this flow.
        // Weighted sampling (if flow is split here).
        double sampledWeight = random(sentNode[node][flow]);
        packet_t sum = 0;
        port_t sampledPort = -1;
        for (const auto sw : views::iota(0, NUM_SWITCHES)) {
            port_t port = port_of(node, sw);
            const auto w = sentPort[port][flow];
            if (sampledWeight >= sum && sampledWeight < sum + w) sampledPort = port;
            sum += w;
        }
        assert(sampledPort >= 0);
        node_t destNode = TOPOLOGY[phase][sampledPort];
        samplePortTransfer(flow, destNode, sentNode[node][flow], recv[destNode][flow]);
    }

    // Add ingress
    for (const auto f : views::iota(0, number_of_flows)) {
        const node_t n = flow_ingress[f];
        packet_t amount = trafficGetFlow(f, gCurrentStep);
        gNodeBuffers[n][f] += amount;
        sampleIngressAdded(f, amount);
    }
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
    for (flow_t flow = 0; flow < number_of_flows; ++flow) {
        std::cout << "sampleLatency[" << flow << "]; ";
    }
    std::cout << "\n";
}

void print_sampling_line(int sample_id) {
    std::cout << sample_id << "; ";
    for (flow_t flow = 0; flow < number_of_flows; ++flow) {
        std::cout << sampleLatency[flow] << "; ";
    }
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
