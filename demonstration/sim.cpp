#include <cassert>
#include "sim-model4.hpp"
#include <ranges>
namespace views = std::views;
#include <cmath>
#include <random>
#include <iostream>

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

const Flow FLOWS[NUM_FLOWS] = ROSSA_GEN_FLOWS;

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
int32_t samplePortPhasePosition[NUM_FLOWS]; // The position of the packet in the flow (num packets before it)
node_t sampleNode[NUM_FLOWS]; // The node it is in.
// port_t samplePort[NUM_FLOWS]; // The port it is in.
// phase_t samplePhase[NUM_FLOWS]; // phase it is to be sent in.
int sampleLatency[NUM_FLOWS]; // The result latency.

constexpr port_t port_of(node_t node, switch_t sw) { return node * NUM_SWITCHES + sw; }
constexpr node_t port_owner(port_t port) { return port / NUM_SWITCHES; }

void pushBuffers() {
    for (const auto node : views::iota(0, NUM_NODES)) {
        extPushBuffers(node, gNodeBuffers[node]);
    }
}

void ON_CONSTRUCT() {
    packet_t nodeData[NUM_NODES] = ROSSA_GEN_NODE_CAPACITIES;
    packet_t portData[NUM_PORTS] = ROSSA_GEN_PORT_BANDWIDTHS;
    node_t topoData[NUM_PORTS];
    int32_t i = 0;
    // Copy Model Parameters
    extBasicParams(NUM_PHASES, NUM_NODES, NUM_FLOWS, NUM_PORTS);

    // portData = PORT_CAPACITIES;
    for (const auto node : views::iota(0, NUM_NODES)) { nodeData[node] = NODE_CAPACITIES[node]; }
    extNodeCapacities(nodeData);

    // portData = PORT_BANDWIDTHS;
    for (const auto port : views::iota(0, NUM_PORTS)) { portData[port] = PORT_BANDWIDTHS[port]; }
    extPortBandwidths(portData);

    // topoData = PORT_OWNER;
    for (const auto port : views::iota(0, NUM_PORTS)) { topoData[port] = port_owner(port); }
    extPushPortOwners(topoData);

    // Copy Flows
    for (i = 0; i < NUM_FLOWS; ++i) {
        extPushFlow(i, FLOWS[i].ingress, FLOWS[i].egress, FLOWS[i].amount);
    }
    // Copy Topology
    for (i = 0; i < NUM_PHASES; ++i) {
        // topoData = TOPOLOGY[i];
        for (const auto port : views::iota(0, NUM_PORTS)) { topoData[port] = TOPOLOGY[i][port]; }
        extPushTopology(i, topoData);
    }
    extSetup();
}

void ON_BEGIN() {
    extBegin();
    // technically not necessary here, but the principle,
    // because is otherwise only updated at the end of steps.
    pushBuffers();
}


/*** CONSTRAINTS ***/

bool verifyScheduler() {
    // Ensure getChoice is ready.
    extPrepareChoices();

    // ingress traffic must always be routed to a valid owned port!
    for (const auto f : views::iota(0, NUM_FLOWS)) {
        for (const auto i : views::iota(0, NUM_PHASES)) {
            // Check all nodes make valid choices.
            for (const auto n : views::iota(0, NUM_NODES)) {
                /*const ScheduleChoice choice = getChoice(i, n, f);
                const node_t bufferOwner = port_owner(choice.port);
                if (bufferOwner != n) {
                    return false;
                }*/
            }
        }
    }
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
        samplePortPhasePosition[f] = -1;
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
            node_t n = FLOWS[f].ingress;
            // We already just did this as part of normal scheduling:
            //    PortBuffers[n][f] += amount;
            // So add our now non-positive ingress position to the amount stored.
            // E.g. if index is now -1 then we were the were last to make it. -6 we are the 6th last etc.
            samplePortPhasePosition[f] = gNodeBuffers[n][f] + sampleIntroIndex[f];
            sampleEntryStep[f] = gCurrentStep;
            sampleNode[f] = n;

            // const ScheduleChoice choice = getChoice(gCurrentPhase, FLOWS[f].ingress, f);
            // samplePort[f] = choice.port;
            // samplePhase[f] = choice.phase;
        }
    }
}
/*
void samplePortTransfer(phase_t i, flow_t f, port_t pSender, node_t destNode, packet_t amount) {
    if (pSender != samplePort[f]) return; // The sampled packet is not here.
    if (i != samplePhase[f]) return; // It is not sent in this phase.
    if (sampleLatency[f] == -1 && samplePortPhasePosition[f] >= 0) {
        // We have not sampled the final latency value yet and have been injected.
        samplePortPhasePosition[f] -= amount;
        if (samplePortPhasePosition[f] < 0) {
            // The packet leaves the port.
            if (destNode == FLOWS[f].egress) {
                // Packet leaves network
                sampleLatency[f] = gCurrentStep - sampleEntryStep[f];
                samplePortPhasePosition[f] = -1;
            } else {
                // Goes to another port
                const ScheduleChoice choice = getChoice(i, destNode, f);
                // We already did: recv[choice.phase][choice.port][f] += sent[i][pSender][f];
                // Our new position is how much is there now "plus our negative position" since we subtracted above.
                // 0-index. If 10 packets was added, and were -10 then we are the first, if -9 we are the last.
                samplePortPhasePosition[f] = gPortBuffers[choice.phase][choice.port][f] + samplePortPhasePosition[f];
                samplePort[f] = choice.port;
                samplePhase[f] = choice.phase;
            }
        }
    }
}
*/

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
    for (const auto n : views::iota(0, NUM_NODES)) sum += packetsAtNode(n);
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
            samplePortPhasePosition[f] = gPortBuffers[sampleChoice.phase][sampleChoice.port][f] +
                samplePortPhasePosition[f];
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

void simulatePhase() {
    // The current sending phase.
    const phase_t i = gCurrentPhase;
    packet_t sentPort[NUM_PORTS][NUM_FLOWS]{};
    packet_t recv[NUM_NODES][NUM_FLOWS]{};
    packet_t sentNode[NUM_NODES][NUM_FLOWS]{};

    extPrepareChoices();

    // Calculate sent (only relevant for current phase 'i')
    for (const auto node : views::iota(0, NUM_NODES)) {
        double schedule[NUM_FLOWS][NUM_SWITCHES]{};
        for (const auto flow : views::iota(0, NUM_FLOWS)) {
            get_normalised_schedule(node, flow, i, schedule[flow]);
        }
        for (const auto sw : views::iota(0, NUM_SWITCHES)) {
            port_t p = port_of(node, sw);
            double sum = 0;
            for (const auto flow : views::iota(0, NUM_FLOWS)) {
                sum += schedule[flow][sw];
            }
            double flow_rate = sum > 0.0 ? fmin(1.0, PORT_BANDWIDTHS[p] / sum) : 0.0;
            packet_t portSending = 0;
            for (const auto flow : views::iota(0, NUM_FLOWS)) {
                const auto sending = static_cast<packet_t>(trunc(schedule[flow][sw] * flow_rate));
                portSending += sending;
                sentPort[p][flow] = sending;
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
            const node_t destNode = TOPOLOGY[i][pSender]; // Which node is the receiver
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
    for (const auto f : views::iota(0, NUM_FLOWS)) {
        // samplePortTransfer(i, f, samplePort[f], TOPOLOGY[i][samplePort[f]], sent[i][samplePort[f]][f]);
    }

    ROSSA_GEN_SCHEDULE_TOGGLE

    // Add ingress
    for (const auto f : views::iota(0, NUM_FLOWS)) {
        const node_t n = FLOWS[f].ingress;
        ROSSA_DEMAND_INJECTION
        gNodeBuffers[n][f] += amount;
        sampleIngressAdded(f, amount);
    }
    updateValidState();
    nextPhase();
    pushBuffers();
}

void output_header() {
    std::cout << "step; gDidOverflow; ";
    for (const auto node : views::iota(0, NUM_NODES)) {
        std::cout << "packetsAtNode(" << node << "); ";
    }
    for (const auto port : views::iota(0, NUM_PORTS)) {
        std::cout << "portUtilization(" << port << "); ";
    }
    std::cout << "\n";
}

void output(phase_t phase) {
    std::cout << phase << "; " << std::boolalpha << gDidOverflow << "; ";
    for (const auto node : views::iota(0, NUM_NODES)) {
        std::cout << packetsAtNode(node) << "; ";
    }
    for (const auto port : views::iota(0, NUM_PORTS)) {
        std::cout << portUtilization(port) << "; ";
    }
    std::cout << "\n";
}

int main() {
    ON_CONSTRUCT();
    ON_BEGIN();
    assert(verifyConstraints());
    setup();
    int t = 0;
    output_header();
    output(t);
    int target = 500;
    while (t <= target) {
        t++;
        simulatePhase();
        output(t);
    }
}
