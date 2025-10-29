#include "ext.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

Network network;

void Buffers::pushBuffers(node_t node, packet_t *data) {
    const size_t start = node * flows_;
    std::copy_n(data, flows_, &values_[start]);
}

void extPushNetwork(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_switches,
                    const int32_t *node_capacities, const int32_t *port_bandwidth,
                    const node_t* flow_ingress, const node_t* flow_egress) {
    network.parameters = Parameters{num_phases, num_nodes, num_flows, num_switches};
    network.parameters.resizeLimits();
    network.flows.resize(num_flows);
    network.buffers = Buffers(num_nodes, num_flows);

    network.topology = Topology(network.parameters);
    network.topology.resizeLimits();

    for (node_t node = 0; node < num_nodes; ++node) {
        network.parameters.capacities[node] = node_capacities[node];
    }
    for (port_t port = 0; port < network.parameters.num_ports(); ++port) {
        network.parameters.bandwidths[port] = port_bandwidth[port];
    }
    for (flow_t flow = 0; flow < num_flows; ++flow) {
        network.flows[flow] = Flow{flow_ingress[flow], flow_egress[flow]};
    }
}

void extPushTopology(phase_t phase_i, const node_t* targets) {
    network.topology.pushTopology(phase_i, targets);
}

void extSchedulerInit() {
    network.buffers.fill(0);
    scheduler_init();
}

void extPushBuffers(node_t node, packet_t *data) {
    network.buffers.pushBuffers(node, data);
}

void extPrepareChoices() {
    customPrepareChoices();
}

void extGetScheduleChoice(node_t node, flow_t flow, phase_t phase_i, switch_t sw, packet_t& choice_weight) {
    customGetScheduleChoice(node, flow, phase_i, sw, choice_weight);
}
