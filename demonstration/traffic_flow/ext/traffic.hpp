#pragma once

#include <cstdint>

namespace rossa::traffic {
using packet_t = int32_t;
using node_t = int32_t;

struct Flow {
    node_t ingress;  // Node packets enter the network.
    node_t egress;   // Node they will egress at.
    packet_t amount; // Number of packets entering each phase.
};

struct Parameters {
    int32_t num_nodes = 0;
};

extern Parameters parameters;
}

#ifdef __cplusplus
extern "C" {
// Core interface
void trafficBasicParams(int32_t num_nodes);
void trafficSetup(); // Called after model construction in UPPAAL. Calls customSetup()
void trafficBegin(); // Called before each query. Calls customBegin()
void trafficPrepareChoices();
void trafficGetFlow(int timestep, rossa::traffic::node_t ingress, rossa::traffic::node_t egress, rossa::traffic::packet_t& amount);


// Trafic flow generators must implement:

// Called after UPPAAL model loaded (or changed).
// The call is made after the network parameters have been set.
void customTrafficSetup();

// Called before each UPPAAL query is run.
void customTrafficBegin();

// Called once for each simulation step before calls to customGetScheduleChoice is made.
void customTrafficPrepareChoices();

void customTrafficGetFlow(int timestep, rossa::traffic::node_t ingress, rossa::traffic::node_t egress, rossa::traffic::packet_t& amount);
}
#endif




