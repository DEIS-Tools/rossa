#pragma once

#include <cstdint>
#include <vector>

namespace rossa::traffic {
using packet_t = int32_t;
using node_t = int32_t;
using flow_t = int32_t;

struct Flow {
    node_t ingress;  // Node packets enter the network.
    node_t egress;   // Node they will egress at.
};

struct Parameters {
    int32_t num_nodes = 0;
};

extern Parameters parameters;
}


#ifdef __cplusplus
extern "C" {
    // Core interface
    void trafficInit(int32_t num_nodes, int32_t& num_flows, rossa::traffic::node_t* ingress_nodes, rossa::traffic::node_t* egress_nodes);
    void trafficGetFlow(rossa::traffic::flow_t flow, int timestep, rossa::traffic::packet_t& amount);
}

#endif

// Trafic flow generators must implement:
// Called before each UPPAAL query is run.
const std::vector<rossa::traffic::Flow>& custom_init_flows();
// Called for each flow and time step
rossa::traffic::packet_t custom_get_flow_demand(rossa::traffic::flow_t flow, int timestep);