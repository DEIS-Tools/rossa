/*** MODEL ***/
const int NUM_PHASES = <<NUM_PHASES>>;
const int NUM_NODES = <<NUM_NODES>>;
const int NUM_FLOWS = <<NUM_FLOWS>>;
const int NUM_SWITCHES = <<NUM_SWITCHES>>;
const int NUM_PORTS = NUM_NODES * NUM_SWITCHES;
const int BUFFER_SIZE = NUM_NODES * NUM_FLOWS;
const int SCHEDULE_SIZE = NUM_NODES * NUM_FLOWS * (NUM_SWITCHES + 1);

typedef int[-1000000,1000000] packet_t;
typedef int[0,NUM_PHASES-1] phase_t;
typedef int[0,NUM_NODES-1] node_t;
typedef int[0,NUM_FLOWS-1] flow_t;
typedef int[0,NUM_SWITCHES-1] switch_t;
typedef int[-1,NUM_SWITCHES-1] switch_or_dummy_t;
typedef int[0,NUM_PORTS-1] port_t;

<<FLOW_STRUCT>>

const packet_t NODE_CAPACITIES[node_t] = <<GEN_NODE_CAPACITIES>>;

const packet_t PORT_BANDWIDTHS[port_t] = <<GEN_PORT_BANDWIDTHS>>;

const node_t TOPOLOGY[NUM_PHASES][port_t] = <<GEN_TOPOLOGY>>;

const Flow FLOWS[flow_t] = <<GEN_FLOWS>>;

/*** STATE ***/
bool gDidOverflow = false;  // Whether any port at any time overflowed
int gCurrentPhase = 0;      // Current phase of the system (will cycle).
int gCurrentStep = 0;  // Non-cyclic phase step counter.
int gCurrentFlowStep = 0;
packet_t gNodeBuffers[BUFFER_SIZE];
packet_t gPortSent[port_t];
meta packet_t maxSendFromPortInPhase = 0;

// Sampling
bool sampling = true;  // You need to disable sampling to perform verification.
int sampleIntroIndex[flow_t]; // The sampled packets place in queue outside network (num rounds before it starts)
int sampleEntryStep[flow_t];
int32_t sampleNodePosition[flow_t]; // The position of the packet in the flow (num packets before it)
int sampleNode[flow_t]; // The node it is in.
int sampleLatency[flow_t];  // The result latency.

port_t port_of(node_t node, switch_t sw) { return node * NUM_SWITCHES + sw; }
node_t port_owner(port_t port) { return port / NUM_SWITCHES; }

packet_t get_buffer(node_t node, flow_t flow) {
    return gNodeBuffers[node * NUM_FLOWS + flow];
}
void set_buffer(node_t node, flow_t flow, packet_t value) {
    gNodeBuffers[node * NUM_FLOWS + flow] = value;
}

/*** EXT INTERFACE ***/
import "./<<EXT_NAME>>" {
    void extPushNetwork(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_switches,
                        packet_t& capacities[NUM_NODES], packet_t& bandwidths[NUM_PORTS]);
    void extPushTopology(int32_t i, node_t& data[NUM_PORTS]);
    void extPushFlow(int32_t i, node_t ingress, node_t egress);
    void extSchedulerInit();
    void extGetScheduleChoiceAll(phase_t phase, packet_t& buffers[BUFFER_SIZE], packet_t& schedule_choice_output[SCHEDULE_SIZE]);
};

void __ON_CONSTRUCT__() {
    packet_t nodeData[node_t];
    packet_t portData[port_t];
    node_t topoData[port_t];

    // Copy network parameters and content to scheduler
    nodeData = NODE_CAPACITIES;
    portData = PORT_BANDWIDTHS;
    extPushNetwork(NUM_PHASES, NUM_NODES, NUM_FLOWS, NUM_SWITCHES, nodeData, portData);
    for (flow : flow_t) {
        extPushFlow(flow, FLOWS[flow].ingress, FLOWS[flow].egress);
    }
    // Topology is 2d-array, so copy piece by piece
    for (phase : phase_t) {
        topoData = TOPOLOGY[phase];
        extPushTopology(phase, topoData);
    }
    extSchedulerInit();
}

void __ON_BEGIN__() {
    // Let the scheduler initialize itself
    extSchedulerInit();
}

/*** CONSTRAINTS ***/

bool verifyTopology() {
    // Check no self-flows
    for (flow : flow_t) {
       if (FLOWS[flow].ingress == FLOWS[flow].egress) {
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
    if (sampling) {
        // Hardcoded assume 50 steps for good enough stabilisation
        const double fStepsToStable = 50.0;
        for (flow : flow_t) {
            sampleLatency[flow] = -1;
            sampleEntryStep[flow] = -1;
            sampleNode[flow] = -1;
            sampleNodePosition[flow] = -1;
            sampleIntroIndex[flow] = fint(fStepsToStable + random(fStepsToStable));
        }
    }
}

/** SAMPLING **/

double maxSampleLatency() {
    double max = 0.0;
    for (flow : flow_t) {
        max = fmax(max, sampleLatency[flow]);
    }
    return max;
}

double averageSampleLatency() {
    flow_t flow;
    double total = 0;
    for (flow : flow_t) {
        total = total + sampleLatency[flow];
    }
    return total / NUM_FLOWS;
}

void sampleIngressAdded(flow_t flow, packet_t amount) {
    // If the sampling packet has not entered the network yet.
    if (sampleIntroIndex[flow] >= 0) {
        sampleIntroIndex[flow] -= 1;
        if (sampleIntroIndex[flow] < 0) {
            // Enter the network.
            const node_t node = FLOWS[flow].ingress;
            // We already just did this as part of normal scheduling:
            //    PortBuffers[choice.phase][choice.port][flow] += amount;
            // So add our now non-positive ingress position to the amount stored.
            // E.g. if index is now -1 then we were the were last to make it. -6 we are the 6th last etc.
            sampleNodePosition[flow] = get_buffer(node, flow) + fint(amount * random(1));
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
                sampleNodePosition[flow] = (sampleNodePosition[flow] * amountDestNode) / amountSendNode;
                // Our new position is how much is there now "plus our negative position" since we subtracted above.
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
    flow_t flow;
    packet_t total = 0;
    for (flow : flow_t) {
        total += get_buffer(node, flow);
    }
    return total;
}

packet_t totalPacketsBuffered() {
    return sum(node : node_t) packetsAtNode(node);
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

bool hasOverflow() {
    for (node : node_t) {
        if (!validNodeState(node)) {
            return false;
        }
    }
    return true;
}

bool updateValidState() {
    for (node : node_t) {
        if (!validNodeState(node)) {
            gDidOverflow = true;
            return false;
        }
    }
    return true;
}

void simulatePhase() {
    // The current sending phase.
    const phase_t phase = gCurrentPhase;
    packet_t sentPort[port_t][flow_t];
    packet_t recv[node_t][flow_t];
    packet_t sentNode[node_t][flow_t];
    double schedule[flow_t][switch_t];

    packet_t schedule_choice_output[SCHEDULE_SIZE];
    extGetScheduleChoiceAll(phase, gNodeBuffers, schedule_choice_output);

    // Calculate sent (only relevant for current phase 'i')
    for (node: node_t) {
        // Reset schedule
        for (flow : flow_t) {
            for (sw: switch_t) {
                schedule[flow][sw] = 0;
            }
        }

        for (flow : flow_t) {
            packet_t buffered = get_buffer(node, flow);
            packet_t weights[switch_t];
            double s = schedule_choice_output[(node * NUM_FLOWS + flow) * (NUM_SWITCHES + 1)];  // a dummy switch to allow not attempting to send all buffered packets in the flow.
            for (sw: switch_t) {
                packet_t choice_weight = schedule_choice_output[(node * NUM_FLOWS + flow) * (NUM_SWITCHES + 1) + sw + 1];
                weights[sw] = choice_weight;
                s = s + choice_weight;
            }
            for (sw: switch_t) {
                schedule[flow][sw] = s == 0 ? 0.0 : buffered * (weights[sw] / s);
            }
        }
        for (sw: switch_t) {
            port_t p = port_of(node, sw);
            packet_t portSending = 0;

            // If port is a self-loop in the current phase, just keep the packets. (This is to avoid issues with latency sampling).
            if (TOPOLOGY[phase][p] != node) {
                double s = 0;
                double flow_rate = 0.0;
                bool stop = false;
                for (flow : flow_t) {
                    s = s + schedule[flow][sw];
                }
                if (s > 0.0) {
                    flow_rate = fmin(1.0, PORT_BANDWIDTHS[p] / s);
                }
                for (flow : flow_t) {
                    const packet_t sending = fint(trunc(schedule[flow][sw] * flow_rate));
                    portSending += sending;
                    sentPort[p][flow] = sending;
                }
                // If we send less that bandwidth due to rounding down to integer, add packets to the flows with the largest rounding errors.
                if (flow_rate != 1) while (!stop && portSending < PORT_BANDWIDTHS[p]) {
                    double max_diff = 0;
                    flow_t flow_with_max_diff = 0;
                    bool found = false;
                    for (flow : flow_t) {
                        double diff = schedule[flow][sw] * flow_rate - sentPort[p][flow];
                        if (sentPort[p][flow] < schedule[flow][sw] && diff > max_diff) {
                            max_diff = diff;
                            flow_with_max_diff = flow;
                            found = true;
                        }
                    }
                    if (!found) {
                        // break;  // If no flows can add packets, stop.
                        stop = true;
                    } else {
                        portSending += 1;
                        sentPort[p][flow_with_max_diff] += 1;
                    }
                }
            }
            gPortSent[p] = portSending;
            maxSendFromPortInPhase = portSending > maxSendFromPortInPhase ? portSending : maxSendFromPortInPhase;
        }
        for (flow : flow_t) {
            for (sw : switch_t) {
                sentNode[node][flow] += sentPort[port_of(node, sw)][flow];
            }
        }
    }
    // Calculate received
    for (flow : flow_t) {                               // For all flows
        for (pSender : port_t) {                        // For any possible port sender
            node_t destNode = TOPOLOGY[phase][pSender]; // Which node is the receiver
            if (destNode != FLOWS[flow].egress) {       // Egress means packets leave.
                recv[destNode][flow] += sentPort[pSender][flow];
            }
        }
    }
    // Update with send/recv
    for (node : node_t) {
        for (flow : flow_t) {
            const packet_t toAdd = recv[node][flow] - sentNode[node][flow];
            set_buffer(node, flow, get_buffer(node, flow) + toAdd);
        }
    }

    if (sampling) {
        // Must be here after buffers are modified, but before new ingress.
        for (flow : flow_t) {
            if (sampleNode[flow] != -1 &&  // Sample for this flow did not yet ingress network.
                sentNode[sampleNode[flow]][flow] != 0) { // Nothing sent on this flow.
                // Weighted sampling (if flow is split here).
                node_t node = sampleNode[flow];
                double sampledWeight = random(sentNode[node][flow]);
                packet_t s = 0;
                port_t sampledPort = 0;
                node_t destNode = 0;
                for (sw: switch_t) {
                    port_t port = port_of(node, sw);
                    const packet_t w = sentPort[port][flow];
                    if (sampledWeight >= s && sampledWeight < s + w) sampledPort = port;
                    s += w;
                }
                destNode = TOPOLOGY[phase][sampledPort];
                samplePortTransfer(flow, destNode, sentNode[node][flow], recv[destNode][flow]);
            }
        }
    }

    // Add ingress
    for (flow : flow_t) {
        const node_t node = FLOWS[flow].ingress;
        packet_t amount = FLOWS[flow].<<AMOUNT>>;
        set_buffer(node, flow, get_buffer(node, flow) + amount);
        if (sampling) {
            sampleIngressAdded(flow, amount);
        }
    }
    updateValidState();
    nextPhase();
}
