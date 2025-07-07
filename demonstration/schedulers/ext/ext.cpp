#include "ext.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

Network network;

void Buffers::pushBuffers(node_t node, packet_t *data) {
    const size_t start = node * flows_;
    std::copy_n(data, flows_, &values_[start]);
}

void extBasicParams(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_ports) {
    network.parameters = Parameters{num_phases, num_nodes, num_flows, num_ports};
    network.parameters.resizeLimits();
    network.flows.resize(num_flows);
    network.buffers = Buffers(num_nodes, num_flows);

    network.topology.num_phases = num_phases;
    network.topology.num_ports = num_ports;
    network.topology.num_nodes = num_nodes;
    network.topology.resizeLimits();
}

void extBegin() {
    network.buffers.fill(0);
    customBegin();
}

void extSetup() {
    customSetup();
}

void extPrepareChoices() {
    customPrepareChoices();
}

void extGetScheduleChoice(port_t port, flow_t flow, phase_t phase_i, int step, packet_t& choice_weight) {
    customGetScheduleChoice(port, flow, phase_i, step, choice_weight);
}

void extNodeCapacities(const packet_t *data) {
    auto &p = network.parameters;
    for (int32_t i = 0; i < p.num_nodes; ++i) {
        p.capacities[i] = data[i];
    }
}
void extPortBandwidths(const packet_t *data) {
    auto &p = network.parameters;
    for (int32_t i = 0; i < p.num_ports; ++i) {
        p.bandwidths[i] = data[i];
    }
}

void extPushFlow(int32_t i, node_t ingress, node_t egress, packet_t amount) {
    auto &flows = network.flows;
    flows[i] = Flow{ingress, egress, amount};
}

void extPushPortOwners(node_t *owners) {
    network.topology.pushOwners(owners);
}

void extPushTopology(phase_t phase_i, node_t *targets) {
    network.topology.pushTopology(phase_i, targets);
}

int32_t extGetPacketsInNetwork() {
    int32_t sum = 0;
    const auto &params = network.parameters;
    for (node_t node = 0; node < params.num_nodes; ++node) {
        for (flow_t flow = 0; flow < params.num_flows; ++flow) {
            sum += network.buffers(node, flow);
        }
    }
    return sum;
}

void extPushBuffers(node_t node, packet_t *data) {
    network.buffers.pushBuffers(node, data);
}
