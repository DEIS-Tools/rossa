#include "traffic.hpp"

#include <cassert>

namespace rossa::traffic {
    Parameters parameters;
}

using namespace rossa::traffic;

void trafficInit(int32_t num_nodes, int32_t& num_flows, node_t* ingress_nodes, node_t* egress_nodes) {
    parameters = Parameters{num_nodes};
    const auto& flows = custom_init_flows();
    num_flows = flows.size();
    assert(num_flows <= num_nodes * num_nodes);
    num_flows = std::min(num_flows, num_nodes * num_nodes);
    for (int32_t flow = 0; flow < num_flows; flow++) {
        ingress_nodes[flow] = flows[flow].ingress;
        egress_nodes[flow] = flows[flow].egress;
    }
}

void trafficGetFlow(flow_t flow, int timestep, packet_t& amount) {
    amount = custom_get_flow_demand(flow, timestep);
}
