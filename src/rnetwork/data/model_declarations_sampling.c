/*** MODEL ***/
const int NUM_PHASES = <<NUM_PHASES>>;
const int NUM_NODES = <<NUM_NODES>>;
const int MAX_FLOWS = (NUM_NODES * NUM_NODES);
const int NUM_SWITCHES = <<NUM_SWITCHES>>;
const int NUM_PORTS = NUM_NODES * NUM_SWITCHES;

typedef int[-1000000,1000000] packet_t;
typedef int[0,NUM_PHASES-1] phase_t;
typedef int[0,NUM_NODES-1] node_t;
typedef int[0,MAX_FLOWS-1] flow_t;
typedef int[0,NUM_SWITCHES-1] switch_t;
typedef int[0,NUM_PORTS-1] port_t;

const packet_t NODE_CAPACITIES[node_t] = <<GEN_NODE_CAPACITIES>>;

const packet_t PORT_BANDWIDTHS[port_t] = <<GEN_PORT_BANDWIDTHS>>;

const node_t TOPOLOGY[NUM_PHASES][port_t] = <<GEN_TOPOLOGY>>;


/*** STATE ***/

// Filled out by traffic generator
int32_t number_of_flows = 0;
node_t flow_ingress[flow_t];
node_t flow_egress[flow_t];

bool gDidOverflow = false;  // Whether any port at any time overflowed
int gCurrentPhase = 0;      // Current phase of the system (will cycle).
int gCurrentStep = 0;  // Non-cyclic phase step counter.
packet_t gNodeBuffers[node_t][flow_t];
packet_t gPortSent[port_t];
meta packet_t maxSendFromPortInPhase = 0;

// Sampling
int sampleIntroIndex[flow_t]; // The sampled packets place in queue outside network (num rounds before it starts)
int sampleEntryStep[flow_t];
int32_t sampleNodePosition[flow_t]; // The position of the packet in the flow (num packets before it)
int sampleNode[flow_t]; // The node it is in.
int sampleLatency[flow_t];  // The result latency.

port_t port_of(node_t node, switch_t sw) { return node * NUM_SWITCHES + sw; }
node_t port_owner(port_t port) { return port / NUM_SWITCHES; }

/*** EXT INTERFACE ***/
import "./<<EXT_NAME>>" {
    void extPushNetwork(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_switches,
                        packet_t& capacities[NUM_NODES], packet_t& bandwidths[NUM_PORTS],
                        node_t& ingress_nodes[MAX_FLOWS], node_t& egress_nodes[MAX_FLOWS]);
    void extPushTopology(int32_t i, node_t& data[NUM_PORTS]);
    void extPushBuffers(node_t node, packet_t& data[MAX_FLOWS]);
    void extSchedulerInit();
    void extPrepareChoices();
    packet_t extGetScheduleChoice(port_t port, flow_t flow, phase_t phase_i, int step);
};
import "./<<TRAFFIC_EXT_NAME>>" {
    // Core interface
    void trafficInit(int32_t num_nodes, int32_t& num_flows, node_t& ingress_nodes[MAX_FLOWS], node_t& egress_nodes[MAX_FLOWS]);
    packet_t trafficGetFlow(flow_t flow, int timestep);
};

void __ON_CONSTRUCT__() {
}

void __ON_BEGIN__() {
    packet_t nodeData[node_t];
    packet_t portData[port_t];
    node_t topoData[port_t];

    // Initialize traffic flows
    trafficInit(NUM_NODES, number_of_flows, flow_ingress, flow_egress);

    // Copy network parameters and content to scheduler
    nodeData = NODE_CAPACITIES;
    portData = PORT_BANDWIDTHS;
    extPushNetwork(NUM_PHASES, NUM_NODES, number_of_flows, NUM_SWITCHES, nodeData, portData, flow_ingress, flow_egress);
    // Topology is 2d-array, so copy piece by piece
    for (phase : phase_t) {
        topoData = TOPOLOGY[phase];
        extPushTopology(phase, topoData);
    }
    // Let the scheduler initialize itself
    extSchedulerInit();
}

void pushBuffers() {
    for (node : node_t) {
        extPushBuffers(node, gNodeBuffers[node]);
    }
}

/*** CONSTRAINTS ***/

bool verifyTopology() {
    flow_t flow;
    // Check no self-flows
    for (flow = 0; flow < number_of_flows; ++flow) {
       if (flow_ingress[flow] == flow_egress[flow]) {
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
    const double fStepsToStable = 50.0;

    for (f : flow_t) {
        sampleLatency[f] = -1;
        sampleEntryStep[f] = -1;
        sampleNode[f] = -1;
        sampleNodePosition[f] = -1;
        sampleIntroIndex[f] = fint(fStepsToStable + random(fStepsToStable));
    }
}

/** SAMPLING **/

double maxSampleLatency() {
    flow_t flow;
    double max = 0.0;
    for (flow = 0; flow < number_of_flows; ++flow) {
        max = fmax(max, sampleLatency[flow]);
    }
    return max;
}

double averageSampleLatency() {
    flow_t flow;
    double total = 0;
    for (flow = 0; flow < number_of_flows; ++flow) {
        total = total + sampleLatency[flow];
    }
    return total / number_of_flows;
}

void sampleIngressAdded(flow_t flow, packet_t amount) {
    // If the sampling packet has not entered the network yet.
    if (sampleIntroIndex[flow] >= 0) {
        sampleIntroIndex[flow] -= 1;
        if (sampleIntroIndex[flow] < 0) {
            // Enter the network.
            const node_t node = flow_ingress[flow];
            // We already just did this as part of normal scheduling:
            //    PortBuffers[choice.phase][choice.port][flow] += amount;
            // So add our now non-positive ingress position to the amount stored.
            // E.g. if index is now -1 then we were the were last to make it. -6 we are the 6th last etc.
            sampleNodePosition[flow] = gNodeBuffers[node][flow] + fint(random(amount));
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
            if (destNode == flow_egress[flow]) {
                // Packet leaves network
                sampleLatency[flow] = gCurrentStep - sampleEntryStep[flow];
                sampleNodePosition[flow] = -1;
                sampleNode[flow] = -1;
            } else {
                // Goes to another port
                sampleNodePosition[flow] = (sampleNodePosition[flow] * amountDestNode) / amountSendNode;
                // Our new position is how much is there now "plus our negative position" since we subtracted above.
                // 0-index. If 10 packets was added, and were -10 then we are the first, if -9 we are the last.
                sampleNodePosition[flow] += gNodeBuffers[destNode][flow];
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
    for (flow = 0; flow < number_of_flows; ++flow) {
        total += gNodeBuffers[node][flow];
    }
    return total;
}

packet_t totalPacketsBuffered() {
    return sum(node : node_t) packetsAtNode(node);
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
    for (n : node_t) {
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
    packet_t sentPort[port_t][flow_t];
    packet_t recv[node_t][flow_t];
    packet_t sentNode[node_t][flow_t];
    double schedule[flow_t][switch_t];
    flow_t flow;

    // Compute new traffic flows
    pushBuffers();
    extPrepareChoices();

    // Calculate sent (only relevant for current phase 'i')
    for (node: node_t) {
        for (flow = 0; flow < number_of_flows; ++flow) {
            packet_t buffered = gNodeBuffers[node][flow];
            packet_t weights[switch_t];
            packet_t s = 0;
            for (sw: switch_t) {
                packet_t choice_weight = extGetScheduleChoice(node, flow, phase, sw);
                weights[sw] = choice_weight;
                s += choice_weight;
            }
            for (sw: switch_t) {
                schedule[flow][sw] = s == 0 ? 0 : buffered * (weights[sw] / s);
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
                for (flow = 0; flow < number_of_flows; ++flow) {
                    s = s + schedule[flow][sw];
                }
                if (s > 0.0) {
                    flow_rate = fmin(1.0, PORT_BANDWIDTHS[p] / s);
                }
                for (flow = 0; flow < number_of_flows; ++flow) {
                    const packet_t sending = fint(trunc(schedule[flow][sw] * flow_rate));
                    portSending += sending;
                    sentPort[p][flow] = sending;
                }
                // If we send less that bandwidth due to rounding down to integer, add packets to the flows with the largest rounding errors.
                if (flow_rate != 1) while (!stop && portSending < PORT_BANDWIDTHS[p]) {
                    double max_diff = 0;
                    flow_t flow_with_max_diff = 0;
                    bool found = false;
                    for (flow = 0; flow < number_of_flows; ++flow) {
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
        for (flow = 0; flow < number_of_flows; ++flow) {
            for (sw : switch_t) {
                sentNode[node][flow] += sentPort[port_of(node, sw)][flow];
            }
        }
    }
    // Calculate received
    for (flow = 0; flow < number_of_flows; ++flow) {    // For all flows
        for (pSender : port_t) {                        // For any possible port sender
            node_t destNode = TOPOLOGY[phase][pSender]; // Which node is the receiver
            if (destNode != flow_egress[flow]) {          // Egress means packets leave.
                recv[destNode][flow] += sentPort[pSender][flow];
            }
        }
    }
    // Update with send/recv
    for (node : node_t) {
        for (flow = 0; flow < number_of_flows; ++flow) {
            const packet_t toAdd = recv[node][flow] - sentNode[node][flow];
            gNodeBuffers[node][flow] += toAdd;
        }
    }

    // Must be here after buffers are modified, but before new ingress.
    for (flow = 0; flow < number_of_flows; ++flow) {
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

    // Add ingress
    for (flow = 0; flow < number_of_flows; ++flow) {
        const node_t node = flow_ingress[flow];
        packet_t amount = trafficGetFlow(flow, gCurrentStep);
        gNodeBuffers[node][flow] += amount;
        sampleIngressAdded(flow, amount);
    }
    updateValidState();
    nextPhase();
}
