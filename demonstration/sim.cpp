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
// constexpr int NUM_FLOWS = ROSSA_NUM_FLOWS;
constexpr int NUM_FLOWS = NUM_NODES * (NUM_NODES - 1) / 4;  // TODO: This depends on configured connection_percentage
constexpr int NUM_SWITCHES = ROSSA_NUM_SWITCHES;
constexpr int NUM_PORTS = NUM_NODES * NUM_SWITCHES;

// typedef int[-1000000,1000000] packet_t;
// typedef int[0,NUM_PHASES-1] phase_t;
// typedef int[0,NUM_NODES-1] node_t;
// typedef int[0,NUM_FLOWS-1] flow_t;
// typedef int[0,NUM_PORTS-1] port_t;

// typedef struct {
//   port_t port;    // Port to store incoming packets in
//   phase_t phase;  // Phase to store them for.
// } ScheduleChoice;

// typedef struct {
//   node_t ingress;
//   node_t egress;
//   packet_t amount;
// } Flow;

const packet_t NODE_CAPACITIES[NUM_NODES] = ROSSA_GEN_NODE_CAPACITIES;

const packet_t PORT_BANDWIDTHS[NUM_PORTS] = ROSSA_GEN_PORT_BANDWIDTHS;

const node_t TOPOLOGY[NUM_PHASES][NUM_PORTS] = ROSSA_GEN_TOPOLOGY;

// const Flow FLOWS[NUM_FLOWS] = ROSSA_GEN_FLOWS;
Flow FLOWS[NUM_FLOWS];

/*** Convenience functions/macros ***/
#define loop(num, i, ...) for (const auto i : views::iota(0, num)) { __VA_ARGS__ }
#define for_phases(i, ...) loop(NUM_PHASES, i, __VA_ARGS__)
#define for_nodes(i, ...) loop(NUM_NODES, i, __VA_ARGS__)
#define for_flows(i, ...) loop(NUM_FLOWS, i, __VA_ARGS__)
#define for_switches(i, ...) loop(NUM_SWITCHES, i, __VA_ARGS__)
#define for_ports(i, ...) loop(NUM_PORTS, i, __VA_ARGS__)

constexpr port_t port_of(node_t node, switch_t sw) { return node * NUM_SWITCHES + sw; }
constexpr node_t port_owner(port_t port) { return port / NUM_SWITCHES; }
/*** STATE ***/

bool gDidOverflow = false; // Whether any port at any time overflowed
int gCurrentPhase = 0; // Current phase of the system (will cycle).
int gCurrentStep = 0; // Non-cyclic phase step counter.
packet_t gNodeBuffers[NUM_NODES][NUM_FLOWS]{};
packet_t gPortSent[NUM_PORTS]{};
meta packet_t maxSendFromPortInPhase = 0;

// Sampling
int sampleIntroIndex[NUM_FLOWS]; // The sampled packets place in queue outside network (num packets before it)
int sampleEntryStep[NUM_FLOWS];
int32_t sampleNodePosition[NUM_FLOWS]; // The position of the packet in the flow (num packets before it)
node_t sampleNode[NUM_FLOWS]; // The node it is in.
// port_t samplePort[NUM_FLOWS]; // The port it is in.
// phase_t samplePhase[NUM_FLOWS]; // phase it is to be sent in.
int sampleLatency[NUM_FLOWS]; // The result latency.


void pushBuffers() {
    for_nodes(node, extPushBuffers(node, gNodeBuffers[node]);)
}

void get_traffic_flows() {
    trafficPrepareChoices();
    {
        flow_t flow = 0;
        for_nodes(ingress, for_nodes(egress,
            if (ingress != egress) {
                packet_t amount;
                trafficGetFlow(gCurrentStep, ingress, egress, amount);
                // const flow_t flow = ingress * (NUM_NODES - 1) + egress - (ingress < egress ? 1 : 0);
                if (amount > 0 && flow < NUM_FLOWS) {
                    FLOWS[flow].ingress = ingress;
                    FLOWS[flow].egress = egress;
                    FLOWS[flow].amount = amount;
                    flow++;
                }
            }
        ))
        while (flow < NUM_FLOWS) {
            FLOWS[flow].ingress = 0;
            FLOWS[flow].egress = 1;
            FLOWS[flow].amount = 0;
            flow++;
        }
    }
    // Copy Flows to scheduler
    for_flows(flow, extPushFlow(flow, FLOWS[flow].ingress, FLOWS[flow].egress, FLOWS[flow].amount);)
}

void ON_CONSTRUCT() {
    packet_t nodeData[NUM_NODES] = ROSSA_GEN_NODE_CAPACITIES;
    packet_t portData[NUM_PORTS] = ROSSA_GEN_PORT_BANDWIDTHS;
    node_t topoData[NUM_PORTS];
    // Copy Model Parameters
    extBasicParams(NUM_PHASES, NUM_NODES, NUM_FLOWS, NUM_PORTS);
    trafficBasicParams(NUM_NODES);

    // portData = PORT_CAPACITIES;
    for_nodes(node, nodeData[node] = NODE_CAPACITIES[node];)
    extNodeCapacities(nodeData);

    // portData = PORT_BANDWIDTHS;
    for_ports(port, portData[port] = PORT_BANDWIDTHS[port];)
    extPortBandwidths(portData);

    // topoData = PORT_OWNER;
    for_ports(port, topoData[port] = port_owner(port);)
    extPushPortOwners(topoData);

    // Create and copy Flows
    trafficSetup();
    trafficBegin();
    get_traffic_flows();
    // Copy Flows
    // for_flows(flow, extPushFlow(flow, FLOWS[flow].ingress, FLOWS[flow].egress, FLOWS[flow].amount);)

    // Copy Topology
    for_phases(phase,
        // topoData = TOPOLOGY[phase];
        for_ports(port,
            topoData[port] = TOPOLOGY[phase][port];
        )
        extPushTopology(phase, topoData);
    )
    extSetup();
}

void ON_BEGIN() {
    gDidOverflow = false;
    gCurrentPhase = 0;
    gCurrentStep = 0;
    for_nodes(node, for_flows(flow, gNodeBuffers[node][flow] = 0;))
    for_ports(port, gPortSent[port] = 0;)
    maxSendFromPortInPhase = 0;

    extBegin();
    trafficBegin();
    // technically not necessary here, but the principle,
    // because is otherwise only updated at the end of steps.
    pushBuffers();
}

void get_normalised_schedule(node_t node, flow_t flow, phase_t phase_i, double norm_schedule[NUM_SWITCHES]) {
    packet_t buffered = gNodeBuffers[node][flow];
    packet_t weights[NUM_SWITCHES];
    packet_t sum = 0;
    for (const auto sw : views::iota(0, NUM_SWITCHES)) {
        packet_t choice_weight;
        extGetScheduleChoice(port_of(node,sw), flow, phase_i, gCurrentStep, choice_weight);  // The current step represents the uniqueness of the state: for each new step, the scheduler needs to reconsider the state.
        weights[sw] = choice_weight;
        sum += choice_weight;
    }
    for (const auto sw : views::iota(0, NUM_SWITCHES)) {
        norm_schedule[sw] = sum == 0 ? 0 : buffered * (weights[sw] / static_cast<double>(sum));
    }
}

/*** CONSTRAINTS ***/

bool verifyScheduler() {
    // Ensure getChoice is ready.
    // extPrepareChoices();

    // ingress traffic must always be routed to a valid owned port!
    // Note: This is trivially satisfied for the new model!
    return true;
}

bool verifyTopology() {
    // Check for direct self-loop
    /* NO: Allow self-loop, since Rotornet2024 contains self-loops.
    for (i : phase_t) {
      for (auto p : views::iota(0, NUM_PORTS)) {
        if (TOPOLOGY[i][p] == port_owner(p)) {
          return false;
        }
      }
    }
    */
    // Check no self-flows
    for (const auto f : views::iota(0, NUM_FLOWS)) {
        if (FLOWS[f].ingress == FLOWS[f].egress) {
            return false;
        }
    }
    return true;
}

bool verifyConstraints() {
    return verifyScheduler() && verifyTopology();
}

/*** TRANSITION ***/

void setup() {
    // Hardcoded assume 50 steps for good enough stabilisation
    constexpr double fStepsToStable = 50.0;

    for (const auto f : views::iota(0, NUM_FLOWS)) {
        sampleLatency[f] = -1;
        sampleEntryStep[f] = -1;
        sampleNode[f] = -1;
        sampleNodePosition[f] = -1;
        sampleIntroIndex[f] = static_cast<int>(trunc(FLOWS[f].amount * fStepsToStable + random(70.0)));
    }
}

/** SAMPLING **/

double maxSampleLatency() {
    double max = 0.0;
    for (const auto f : views::iota(0, NUM_FLOWS)) {
        max = fmax(max, sampleLatency[f]);
    }
    return max;
}

double averageSampleLatency() {
    // double total = sum(f : flows_t) sampleLatency[f];
    double total = std::accumulate(std::begin(sampleLatency), std::end(sampleLatency), 0.0);
    return total / NUM_FLOWS;
}

void sampleIngressAdded(flow_t f, packet_t amount) {
    // If the sampling packet has not entered the network yet.
    if (sampleIntroIndex[f] >= 0) {
        sampleIntroIndex[f] = sampleIntroIndex[f] - amount;
        if (sampleIntroIndex[f] < 0) {
            // Enter the network.
            const node_t n = FLOWS[f].ingress;
            // We already just did this as part of normal scheduling:
            //    gNodeBuffers[n][f] += amount;
            // So add our now non-positive ingress position to the amount stored.
            // E.g. if index is now -1 then we were the last to make it. -6 we are the 6th last etc.
            sampleNodePosition[f] = gNodeBuffers[n][f] + sampleIntroIndex[f];
            sampleEntryStep[f] = gCurrentStep;
            sampleNode[f] = n;

            // const ScheduleChoice choice = getChoice(gCurrentPhase, FLOWS[f].ingress, f);
            // samplePort[f] = choice.port;
            // samplePhase[f] = choice.phase;
        }
    }
}

void samplePortTransfer(flow_t f, node_t destNode, packet_t amountSendNode, packet_t amountDestNode) {
    if (sampleLatency[f] == -1 && sampleNodePosition[f] >= 0) {
        // We have not sampled the final latency value yet and have been injected.
        sampleNodePosition[f] -= amountSendNode;
        if (sampleNodePosition[f] < 0) {
            // The packet leaves the port.
            if (destNode == FLOWS[f].egress) {
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

packet_t packetsAtNode(node_t n) {
    return std::accumulate(std::begin(gNodeBuffers[n]), std::end(gNodeBuffers[n]), 0);
}

packet_t totalPacketsBuffered() {
    // return sum(p : port_t) totalPortBuffered(p);
    packet_t sum = 0;
    for_nodes(n, sum += packetsAtNode(n);)
    return sum;
}


/*
void reschedule(phase_t phase) {
    for (const auto f : views::iota(0, NUM_FLOWS)) {
        // Used lower, but C-style initializing needed....
        const packet_t sampleAtNode = port_owner(samplePort[f]);
        const ScheduleChoice sampleChoice = getChoice(phase, sampleAtNode, f);
        const bool sampleChangingPlace = sampleChoice.port != samplePort[f] || sampleChoice.phase != samplePhase[f];

        // We use a extra data "deltas" to avoid re-scheduling interfering with itself.
        // E.g.  otherwise portA may be rescheduled on portB. Then portB is rescheduled to portA etc.
        packet_t deltas[NUM_PHASES][NUM_PORTS];
        for (const auto port : views::iota(0, NUM_PORTS)) {
            const packet_t remaining = gPortBuffers[phase][port][f];
            const ScheduleChoice choice = getChoice(phase, port_owner(port), f);
            assert(port_owner(choice.port) == port_owner(port));
            deltas[choice.phase][choice.port] += remaining;
            deltas[phase][port] -= remaining;
        }

        // Change the buffers now.
        for (const auto port : views::iota(0, NUM_PORTS)) {
            for (const auto newPhase : views::iota(0, NUM_PHASES)) {
                gPortBuffers[newPhase][port][f] += deltas[newPhase][port];
            }
        }

        // NOTE: WARNING: This rescheduling may be arbitrary with respect to latency if multiple
        // ports are rescheduled to this one.
        // DECISION: make the queue position proportional to the shifted packets.

        // Update sampling data.
        // The sampled packet is in the network
        if (sampleChangingPlace && sampleLatency[f] == -1 && sampleEntryStep[f] != -1) {
            // It's new place in the queue is its current place + how much is already in the new destination port/phase-combination.
            // TODO: Recheck this line. Consider what was before.
            sampleNodePosition[f] = gPortBuffers[sampleChoice.phase][sampleChoice.port][f] +
                sampleNodePosition[f];
            samplePort[f] = sampleChoice.port;
            samplePhase[f] = sampleChoice.phase;
        }
    }
}
*/
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
    packet_t sentPort[NUM_PORTS][NUM_FLOWS]{};
    packet_t recv[NUM_NODES][NUM_FLOWS]{};
    packet_t sentNode[NUM_NODES][NUM_FLOWS]{};

    // Compute new traffic flows
    get_traffic_flows();
    extPrepareChoices();

    // Calculate sent (only relevant for current phase 'i')
    for (const auto node : views::iota(0, NUM_NODES)) {
        double schedule[NUM_FLOWS][NUM_SWITCHES]{};
        for (const auto flow : views::iota(0, NUM_FLOWS)) {
            get_normalised_schedule(node, flow, phase, schedule[flow]);
        }
        for (const auto sw : views::iota(0, NUM_SWITCHES)) {
            port_t p = port_of(node, sw);
            packet_t portSending = 0;

            // If port is a self-loop in the current phase, just keep the packets. (This is to avoid issues with latency sampling).
            if (TOPOLOGY[phase][p] != node) {
                double sum = 0;
                for (const auto flow : views::iota(0, NUM_FLOWS)) {
                    sum += schedule[flow][sw];
                }
                double flow_rate = sum > 0.0 ? fmin(1.0, PORT_BANDWIDTHS[p] / sum) : 0.0;

                for (const auto flow : views::iota(0, NUM_FLOWS)) {
                    const auto sending = static_cast<packet_t>(trunc(schedule[flow][sw] * flow_rate));
                    portSending += sending;
                    sentPort[p][flow] = sending;
                }
                // If we send less that bandwidth due to rounding down to integer, add packets to the flows with the largest rounding errors.
                if (flow_rate != 1) while (portSending < PORT_BANDWIDTHS[p]) {
                    double max_diff = 0;
                    flow_t flow_with_max_diff = -1;
                    for (const auto flow : views::iota(0, NUM_FLOWS)) {
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
                // assert(portSending == (PORT_BANDWIDTHS[p] < trunc(sum) ? PORT_BANDWIDTHS[p] : trunc(sum)));
            }
            gPortSent[p] = portSending;
            maxSendFromPortInPhase = portSending > maxSendFromPortInPhase ? portSending : maxSendFromPortInPhase;
        }
        for (const auto flow : views::iota(0, NUM_FLOWS)) {
            for (const auto sw : views::iota(0, NUM_SWITCHES)) {
                sentNode[node][flow] += sentPort[port_of(node, sw)][flow];
            }
        }
    }
    // Calculate received
    for (const auto f : views::iota(0, NUM_FLOWS)) { // For all flows
        for (const auto pSender : views::iota(0, NUM_PORTS)) { // For any possible port sender
            const node_t destNode = TOPOLOGY[phase][pSender]; // Which node is the receiver
            if (destNode != FLOWS[f].egress) { // Egress means packets leave.
                //Add the sent packages to the receiving node.
                recv[destNode][f] += sentPort[pSender][f];
            }
        }
    }
    // Update with send/recv
    for (const auto node : views::iota(0, NUM_NODES)) {
        for (const auto flow : views::iota(0, NUM_FLOWS)) {
            packet_t toAdd = recv[node][flow] - sentNode[node][flow];
            gNodeBuffers[node][flow] += toAdd;
        }
    }

    // Must be here after buffers are modified, but before new ingress.
    for (const auto flow : views::iota(0, NUM_FLOWS)) {
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

    ROSSA_GEN_SCHEDULE_TOGGLE

    // Add ingress
    for (const auto f : views::iota(0, NUM_FLOWS)) {
        const node_t n = FLOWS[f].ingress;
        // ROSSA_DEMAND_INJECTION
        const packet_t amount = FLOWS[f].amount;
        gNodeBuffers[n][f] += amount;
        sampleIngressAdded(f, amount);
    }
    updateValidState();
    nextPhase();
    pushBuffers();
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
    for_flows(flow, std::cout << "sampleLatency[" << flow << "]; ";)
    std::cout << "\n";
}

void print_sampling_line(int sample_id) {
    std::cout << sample_id << "; ";
    for_flows(flow, std::cout << sampleLatency[flow] << "; ";)
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
