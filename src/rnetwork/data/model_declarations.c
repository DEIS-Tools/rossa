/*** MODEL ***/
const int NUM_PHASES = <<NUM_PHASES>>;
const int NUM_NODES = <<NUM_NODES>>;
const int NUM_FLOWS = <<NUM_FLOWS>>;
const int NUM_PORTS = <<NUM_PORTS>>;

typedef int[-1000000,1000000] packet_t;
typedef int[0,NUM_PHASES-1] phase_t;
typedef int[0,NUM_NODES-1] node_t;
typedef int[0,NUM_FLOWS-1] flow_t;
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

const packet_t PORT_CAPACITIES[port_t] = <<GEN_PORT_CAPACITIES>>;

const packet_t PORT_BANDWIDTHS[port_t] = <<GEN_PORT_BANDWIDTHS>>;

const node_t TOPOLOGY[NUM_PHASES][port_t] = <<GEN_TOPOLOGY>>;

const Flow FLOWS[flow_t] = <<GEN_FLOWS>>;

/*** STATE ***/

bool gDidOverflow = false;  // Whether any port at any time overflowed
int gCurrentPhase = 0;      // Current phase of the system (will cycle).
// int gCurrentStep = 0;  // Non-cyclic phase step counter. // Ruins reachability checking.
packet_t gPortBuffers[phase_t][port_t][flow_t];
packet_t gPortSent[port_t];
meta packet_t maxSendFromPortInPhase = 0;

// Sampling
int sampleIntroIndex[flow_t]; // The sampled packets place in queue outside network (num packets before it)
int sampleEntryStep[flow_t];
int32_t samplePortPhasePosition[flow_t]; // The position of the packet in the flow (num packets before it)
port_t samplePort[flow_t]; // The port it is in.
phase_t samplePhase[flow_t]; // phase it is to be sent in.
int sampleLatency[flow_t];  // The result latency.

/*** EXT INTERFACE ***/
import "./<<EXT_NAME>>" {
  void extBasicParams(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_ports);
  void extPortCapacities(packet_t& capacities[NUM_PORTS]);
  void extPortBandwidths(packet_t& bandwidths[NUM_PORTS]);
  void extPushPortOwners(node_t& owners[NUM_PORTS]);
  void extPushBuffers(phase_t phase, port_t port, packet_t& data[NUM_FLOWS]);
  void extPushFlow(int32_t i, node_t ingress, node_t egress, packet_t amount);
  void extPushTopology(int32_t i, node_t& data[NUM_PORTS]);
  packet_t extGetPacketsInNetwork();
  void extSetup();
  void extBegin();
  void extPrepareChoices();
  void extGetScheduleChoice(phase_t phase_i, node_t node, flow_t flow, phase_t &choice_phase, port_t &choice_port);
};

void pushBuffers() {
   for (phase : phase_t) {
      for (port : port_t) {
        extPushBuffers(phase, port, gPortBuffers[phase][port]);
      }
   }
}

void __ON_CONSTRUCT__() {
  packet_t portData[port_t];
  node_t topoData[port_t];
  int32_t i = 0;
  // Copy Model Parameters
  extBasicParams(NUM_PHASES, NUM_NODES, NUM_FLOWS, NUM_PORTS);  
  portData = PORT_CAPACITIES;
  extPortCapacities(portData);
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

ScheduleChoice getChoice(phase_t phase, node_t node, flow_t flow) {
     ScheduleChoice choice;
     extGetScheduleChoice(phase, node, flow, choice.phase, choice.port);
     return choice;
}

/*** CONSTRAINTS ***/

bool verifyScheduler() {
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
  }
  return true;
}

bool verifyTopology() {
  // Check for direct self-loop
  for (i : phase_t) {
    for (p : port_t) {
      if (TOPOLOGY[i][p] == PORT_OWNER[p]) {
        return false;
      }
    }
  }
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

// Sum packets buffered at the port for phase over all flows.
packet_t portBuffered(phase_t i, port_t p) {
  return sum(f : flow_t) gPortBuffers[i][p][f];
}

packet_t totalPortBuffered(port_t p) {
  return sum(i : phase_t) portBuffered(i, p);
}

packet_t totalPacketsBuffered() {
  return sum(p : port_t) totalPortBuffered(p);
}

packet_t packetsAtNode(node_t n) {
  packet_t count = 0;
  for (p : port_t) {
    if (n == PORT_OWNER[p]) count += totalPortBuffered(p);
  }
  return count;
}

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
  }
}

void nextPhase() {
  //gCurrentStep += 1;
  gCurrentPhase += 1;
  if (gCurrentPhase == NUM_PHASES) {
    gCurrentPhase = 0;
  }
}

bool validPortState(port_t p) {
   return (sum(i : phase_t) portBuffered(i, p)) <= PORT_CAPACITIES[p];
}

bool updateValidState() {
  for (p : port_t) {
    if (!validPortState(p)) {
      gDidOverflow = true;
      return false;
    }
  }
  return true;
}

void simulatePhase() {
  // The current sending phase.
  const phase_t i = gCurrentPhase;
  packet_t sent[phase_t][port_t][flow_t];
  packet_t recv[phase_t][port_t][flow_t];

  extPrepareChoices();

  // Calculate sent (only relevant for current phase 'i')
  for (p : port_t) {
    packet_t portSending = 0;
    for (f : flow_t) {
      packet_t fSending = sending(i, p, f);
      portSending += fSending;
      sent[i][p][f] = fSending;
    }
    gPortSent[p] = portSending;
    maxSendFromPortInPhase = portSending > maxSendFromPortInPhase ? portSending : maxSendFromPortInPhase;
  }
  // Calculate received
  for (f : flow_t) { // For all flows
    for (pSender : port_t) {                    // For any possible port sender                    
      node_t destNode = TOPOLOGY[i][pSender]; // Which node is the receiver
      if (destNode != FLOWS[f].egress) {      // Egress means packets leave.
        const ScheduleChoice choice = getChoice(i, destNode, f);
        recv[choice.phase][choice.port][f] += sent[i][pSender][f]; //Add the sent packages to the receiving port of the node.
      }
    }
  }
  // Update with send/recv
  for (j : phase_t) {
    for (p : port_t) {
      for (f : flow_t) {
        const packet_t toAdd = recv[j][p][f] - sent[j][p][f];
        gPortBuffers[j][p][f] += toAdd;
      }
    }
  }

  <<GEN_SCHEDULE_TOGGLE>>

  // Add ingress
  for (f : flow_t) {
    const node_t n = FLOWS[f].ingress;
    const packet_t amount = FLOWS[f].amount;
    const ScheduleChoice choice = getChoice(i, n, f);
    gPortBuffers[choice.phase][choice.port][f] += amount;
  }
  updateValidState();
  nextPhase();
  pushBuffers();
}
