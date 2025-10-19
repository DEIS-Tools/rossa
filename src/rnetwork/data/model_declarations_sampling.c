/*** MODEL ***/
const int NUM_PHASES = <<NUM_PHASES>>;
const int NUM_NODES = <<NUM_NODES>>;
const int NUM_FLOWS = <<NUM_FLOWS>>;
const int NUM_SWITCHES = <<NUM_SWITCHES>>;
const int NUM_PORTS = NUM_NODES * NUM_SWITCHES;

typedef int[-1000000,1000000] packet_t;
typedef int[0,NUM_PHASES-1] phase_t;
typedef int[0,NUM_NODES-1] node_t;
typedef int[0,NUM_FLOWS-1] flow_t;
typedef int[0,NUM_SWITCHES-1] switch_t;
typedef int[0,NUM_PORTS-1] port_t;

typedef struct {
  port_t port;    // Port to store incoming packets in
  phase_t phase;  // Phase to store them for.
} ScheduleChoice;

typedef struct {
  node_t ingress;
  node_t egress;
  packet_t amount;
} Flow;

const node_t PORT_OWNER[port_t] = <<GEN_PORT_OWNER>>;

const packet_t NODE_CAPACITIES[node_t] = <<GEN_NODE_CAPACITIES>>;

const packet_t PORT_BANDWIDTHS[port_t] = <<GEN_PORT_BANDWIDTHS>>;

const node_t TOPOLOGY[NUM_PHASES][port_t] = <<GEN_TOPOLOGY>>;

const Flow FLOWS[flow_t] = <<GEN_FLOWS>>;

/*** STATE ***/

bool gDidOverflow = false;  // Whether any port at any time overflowed
int gCurrentPhase = 0;      // Current phase of the system (will cycle).
int gCurrentStep = 0;  // Non-cyclic phase step counter.
packet_t gNodeBuffers[node_t][flow_t];
packet_t gPortSent[port_t];
meta packet_t maxSendFromPortInPhase = 0;

// Sampling
int sampleIntroIndex[flow_t]; // The sampled packets place in queue outside network (num packets before it)
int sampleEntryStep[flow_t];
int32_t sampleNodePosition[flow_t]; // The position of the packet in the flow (num packets before it)
int sampleNode[flow_t]; // The node it is in.
//port_t samplePort[flow_t]; // The port it is in.
//phase_t samplePhase[flow_t]; // phase it is to be sent in.
int sampleLatency[flow_t];  // The result latency.

port_t port_of(node_t node, switch_t sw) { return node * NUM_SWITCHES + sw; }
node_t port_owner(port_t port) { return port / NUM_SWITCHES; }

/*** EXT INTERFACE ***/
import "./<<EXT_NAME>>" {
  void extBasicParams(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_ports);
  void extNodeCapacities(packet_t& capacities[NUM_NODES]);
  void extPortBandwidths(packet_t& bandwidths[NUM_PORTS]);
  void extPushPortOwners(node_t& owners[NUM_PORTS]);
  void extPushBuffers(node_t node, packet_t& data[NUM_FLOWS]);
  void extPushFlow(int32_t i, node_t ingress, node_t egress, packet_t amount);
  void extPushTopology(int32_t i, node_t& data[NUM_PORTS]);
  packet_t extGetPacketsInNetwork();
  void extSetup();
  void extBegin();
  void extPrepareChoices();
  void extGetScheduleChoice(port_t port, flow_t flow, phase_t phase_i, int step, packet_t& choice_weight);
};

void pushBuffers() {
   for (node : node_t) {
      extPushBuffers(node, gNodeBuffers[node]);
   }
}

void __ON_CONSTRUCT__() {
  packet_t nodeData[node_t];
  packet_t portData[port_t];
  node_t topoData[port_t];
  int32_t i = 0;
  // Copy Model Parameters
  extBasicParams(NUM_PHASES, NUM_NODES, NUM_FLOWS, NUM_PORTS);  
  nodeData = NODE_CAPACITIES;
  extNodeCapacities(nodeData);
  portData = PORT_BANDWIDTHS;
  extPortBandwidths(portData);
  topoData = PORT_OWNER;
  extPushPortOwners(topoData);
  // Copy Flows
  for (i=0; i < NUM_FLOWS; ++i) {
      extPushFlow(i, FLOWS[i].ingress, FLOWS[i].egress, FLOWS[i].amount);
  }
  // Copy Topology
  for (i=0; i < NUM_PHASES; ++i) {
      topoData = TOPOLOGY[i];
      extPushTopology(i, topoData);
  }
  extSetup();
}

void __ON_BEGIN__() {
  extBegin();
  // technically not necessary here, but the principle,
  // because is otherwise only updated at the end of steps.
  pushBuffers();
}

//ScheduleChoice getChoice(phase_t phase, node_t node, flow_t flow) {
//     ScheduleChoice choice;
//     extGetScheduleChoice(phase, node, flow, choice.phase, choice.port);
//     return choice;
//}

/*** CONSTRAINTS ***/

bool verifyScheduler() {
/*
  // Ensure getChoice is ready.
  extPrepareChoices();

  // ingress traffic must always be routed to a valid owned port!
  for (f : flow_t) {
    for (i : phase_t) {
      // Check all nodes make valid choices.
      for (n : node_t) {      
         const ScheduleChoice choice = getChoice(i, n, f);
         node_t bufferOwner = PORT_OWNER[choice.port];
         if (bufferOwner != n) {
           return false;
         }
      }
    }
  }*/
  return true;
}

bool verifyTopology() {
  // Check for direct self-loop
  /* NO: Allow self-loop, since Rotornet2024 contains self-loops.
  for (i : phase_t) {
    for (p : port_t) {
      if (TOPOLOGY[i][p] == PORT_OWNER[p]) {
        return false;
      }
    }
  }
  */
  // Check no self-flows
  for (f : flow_t) {
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
  const double fStepsToStable = 50.0;

  for (f : flow_t) {
    sampleLatency[f] = -1;
    sampleEntryStep[f] = -1;
    sampleNode[f] = -1;
    sampleNodePosition[f] = -1;
    sampleIntroIndex[f] = fint(FLOWS[f].amount *  fStepsToStable + random(70.0));
  }
}

/** SAMPLING **/

double maxSampleLatency() {
  double max = 0.0;
  for (f: flow_t) {
    max = fmax(max, sampleLatency[f]);
  }
  return max;
}

double averageSampleLatency() {
  double total = sum(f : flow_t) sampleLatency[f];
  return total / NUM_FLOWS;
}

void sampleIngressAdded(flow_t f, packet_t amount) {
  // If the sampling packet has not entered the network yet.
  if (sampleIntroIndex[f] >= 0) {
    sampleIntroIndex[f] = sampleIntroIndex[f] - amount;
    if (sampleIntroIndex[f] < 0) {
      // Enter the network.
      const node_t n = FLOWS[f].ingress;
      //const ScheduleChoice choice = getChoice(gCurrentPhase, FLOWS[f].ingress, f);
      // We already just did this as part of normal scheduling:
      //    PortBuffers[choice.phase][choice.port][f] += amount;
      // So add our now non-positive ingress position to the amount stored.
      // E.g. if index is now -1 then we were the were last to make it. -6 we are the 6th last etc.
      sampleNodePosition[f] = gNodeBuffers[n][f] + sampleIntroIndex[f];
      sampleEntryStep[f] = gCurrentStep;
      sampleNode[f] = n;
//      samplePortPhasePosition[f] = gPortBuffers[choice.phase][choice.port][f] + sampleIntroIndex[f];
//      sampleEntryStep[f] = gCurrentStep;
//      samplePort[f] = choice.port;
//      samplePhase[f] = choice.phase;
    }
  }
}

void samplePortTransfer(flow_t f, node_t destNode, packet_t amountSendNode, packet_t amountDestNode) {
  //if (pSender != samplePort[f]) return; // The sampled packet is not here.
  //if (i != samplePhase[f]) return; // It is not sent in this phase.
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
        sampleNodePosition[f] = (sampleNodePosition[f] * amountDestNode) / amountSendNode;
        // Our new position is how much is there now "plus our negative position" since we subtracted above.
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

// Sum packets buffered at the port for phase over all flows.
/*packet_t portBuffered(phase_t i, port_t p) {
  return sum(f : flow_t) gPortBuffers[i][p][f];
}

packet_t totalPortBuffered(port_t p) {
  return sum(i : phase_t) portBuffered(i, p);
}*/

packet_t packetsAtNode(node_t n) {
  return sum(f : flow_t) gNodeBuffers[n][f];
}

packet_t totalPacketsBuffered() {
  return sum(n : node_t) packetsAtNode(n);
}


/*
packet_t sending(phase_t i, port_t p, flow_t f) {
  double dTotal, dFlow, dPacketsToSend;
  const packet_t totalBuffered = portBuffered(i, p);
  if (totalBuffered == 0) {
    return 0;
  }
  dPacketsToSend = fmin(PORT_BANDWIDTHS[p], totalBuffered);
  dTotal = totalBuffered;
  dFlow = gPortBuffers[i][p][f];
  return fint(round((dFlow * (dPacketsToSend / dTotal))));
}

void reschedule(phase_t phase) {
  for (f : flow_t) {
    // Used lower, but C-style initializing needed....
    const packet_t sampleAtNode = PORT_OWNER[samplePort[f]];
    const ScheduleChoice sampleChoice = getChoice(phase, sampleAtNode, f);
    const bool sampleChangingPlace = sampleChoice.port != samplePort[f] || sampleChoice.phase != samplePhase[f];

    // We use a extra data "deltas" to avoid re-scheduling interfering with itself.
    // E.g.  otherwise portA may be rescheduled on portB. Then portB is rescheduled to portA etc.
    packet_t deltas[phase_t][port_t];
    for (port : port_t) {
      packet_t remaining = gPortBuffers[phase][port][f];
      const ScheduleChoice choice = getChoice(phase, PORT_OWNER[port], f);
      assert(PORT_OWNER[choice.port] == PORT_OWNER[port]);
      deltas[choice.phase][choice.port] += remaining;
      deltas[phase][port] -= remaining;
    }

    // Change the buffers now.
    for (port: port_t) {
      for (newPhase : phase_t) {
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
      samplePortPhasePosition[f] = gPortBuffers[sampleChoice.phase][sampleChoice.port][f] + samplePortPhasePosition[f];
      samplePort[f] = sampleChoice.port;
      samplePhase[f] = sampleChoice.phase;
    }
  }
}*/

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

  extPrepareChoices();

  // Calculate sent (only relevant for current phase 'i')
  for (node: node_t) {
      for (flow: flow_t) {
          packet_t buffered = gNodeBuffers[node][flow];
          packet_t weights[switch_t];
          packet_t s = 0;
          for (sw: switch_t) {
            packet_t choice_weight;
            extGetScheduleChoice(port_of(node,sw), flow, phase, gCurrentStep, choice_weight);  // The current step represents the uniqueness of the state: for each new step, the scheduler needs to reconsider the state.
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
              for (flow: flow_t) {
                  s = s + schedule[flow][sw];
              }
              if (s > 0.0) {
                  flow_rate = fmin(1.0, PORT_BANDWIDTHS[p] / s);
              }
              for (flow: flow_t) {
                  const packet_t sending = fint(trunc(schedule[flow][sw] * flow_rate));
                  portSending += sending;
                  sentPort[p][flow] = sending;
              }
              // If we send less that bandwidth due to rounding down to integer, add packets to the flows with the largest rounding errors.
              while (!stop && portSending < PORT_BANDWIDTHS[p]) {
                  double max_diff = 0;
                  flow_t flow_with_max_diff = 0;
                  bool found = false;
                  for (flow: flow_t) {
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
              //assert(portSending == (PORT_BANDWIDTHS[p] < trunc(sum) ? PORT_BANDWIDTHS[p] : trunc(sum)));
          }
          gPortSent[p] = portSending;
          maxSendFromPortInPhase = portSending > maxSendFromPortInPhase ? portSending : maxSendFromPortInPhase;
      }
      for (flow: flow_t) {
          for (sw : switch_t) {
              sentNode[node][flow] += sentPort[port_of(node, sw)][flow];
          }
      }
  }

  /*for (p : port_t) {
    packet_t portSending = 0;
    for (f : flow_t) {
      packet_t fSending = sending(i, p, f);
      portSending += fSending;
      sent[i][p][f] = fSending;
    }
    gPortSent[p] = portSending;
    maxSendFromPortInPhase = portSending > maxSendFromPortInPhase ? portSending : maxSendFromPortInPhase;
  }*/
  // Calculate received
  for (f : flow_t) { // For all flows
    for (pSender : port_t) {                    // For any possible port sender                    
      node_t destNode = TOPOLOGY[phase][pSender]; // Which node is the receiver
      if (destNode != FLOWS[f].egress) {      // Egress means packets leave.
        //const ScheduleChoice choice = getChoice(i, destNode, f);
        //recv[choice.phase][choice.port][f] += sent[i][pSender][f]; //Add the sent packages to the receiving port of the node.
        recv[destNode][f] += sentPort[pSender][f];
      }
    }
  }
  // Update with send/recv
  for (n : node_t) {
    for (f : flow_t) {
      const packet_t toAdd = recv[n][f] - sentNode[n][f];
      gNodeBuffers[n][f] += toAdd;
    }
  }

  // Must be here after buffers are modified, but before new ingress.
  for (flow: flow_t) {
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
  //for (f: flow_t) {
  //    samplePortTransfer(i, f, samplePort[f], TOPOLOGY[i][samplePort[f]], sent[i][samplePort[f]][f]);
  //}

  <<GEN_SCHEDULE_TOGGLE>>

  // Add ingress
  for (f : flow_t) {
    const node_t n = FLOWS[f].ingress;
    <<DEMAND_INJECTION>>
    //const ScheduleChoice choice = getChoice(i, n, f);
    //gPortBuffers[choice.phase][choice.port][f] += amount;
    gNodeBuffers[n][f] += amount;
    sampleIngressAdded(f, amount);
  }
  updateValidState();
  nextPhase();
  pushBuffers();
}
