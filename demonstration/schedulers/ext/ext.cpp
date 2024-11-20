#include "ext.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

Network network;

void Buffers::pushBuffers(phase_t phase, port_t port, packet_t *data) {
    const size_t start = phase * ports_ * flows_ + port * flows_;
    std::copy(data, data + flows_, &values_[start]);
}

void extBasicParams(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_ports) {
    network.parameters = Parameters{num_phases, num_nodes, num_flows, num_ports};
    network.parameters.resizeLimits();
    network.flows.resize(num_flows);
    network.buffers = Buffers(num_phases, num_ports, num_flows);

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

void extGetScheduleChoice(phase_t phase_i, node_t node, flow_t flow, phase_t &choice_phase, port_t &choice_port) {
    customGetScheduleChoice(phase_i, node, flow, choice_phase, choice_port);
}

void extPortCapacities(packet_t *data) {
    auto &p = network.parameters;
    for (int32_t i = 0; i < p.num_ports; ++i) {
        p.capacities[i] = data[i];
    }
}
void extPortBandwidths(packet_t *data) {
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
    for (phase_t phase = 0; phase < params.num_phases; ++phase) {
        for (port_t port = 0; port < params.num_ports; ++port) {
            for (flow_t flow = 0; flow < params.num_flows; ++flow) {
                sum += network.buffers(phase, port, flow);
            }
        }
    }
    return sum;
}

void extPushBuffers(int32_t phase, port_t port, node_t *data) {
    network.buffers.pushBuffers(phase, port, data);
}
