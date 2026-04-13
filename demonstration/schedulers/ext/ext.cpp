#include "ext.hpp"

#include <cassert>
#include <algorithm>
#include <vector>

Network network;

int32_t Buffers::operator()(node_t node, flow_t flow) const {
    return values_[node * flows_ + flow];
}

void Buffers::pushBuffers(node_t node, const packet_t *data) {
    const size_t start = node * flows_;
    std::copy_n(data, flows_, &values_[start]);
}
void Buffers::pushAllBuffers(const packet_t *data) {
    std::copy_n(data, nodes_ * flows_, &values_[0]);
}

void Buffers::fill(const packet_t value) {
    std::fill(values_.begin(), values_.end(), value);
}

uint32_t Buffers::get_buffer_hash() const {
    uint32_t seed = values_.size();
    for(const auto y : values_) {
        auto x = static_cast<uint32_t>(y);
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
}

node_t Topology::operator()(phase_t phase, port_t port) const {
    return topology[phase * num_ports() + port];
}

port_t Topology::next_port_to(node_t src_node, node_t dst_node, phase_t current_phase) const {
    for (phase_t offset = 0; offset < num_phases; offset++) {
        phase_t phase = (current_phase + offset) % num_phases;
        for (switch_t sw = 0; sw < num_switches; sw++) {
            port_t port = port_of(src_node, sw);
            if ((*this)(phase, port) == dst_node) {
                return port;
            }
        }
    }
    assert(false);
    return -1;
}
phase_t Topology::phase_offset_next_connection(node_t src_node, node_t dst_node, phase_t current_phase) const {
    for (phase_t offset = 1; offset <= num_phases; offset++) {
        phase_t phase = (current_phase + offset) % num_phases;
        for (switch_t sw = 0; sw < num_switches; sw++) {
            port_t port = port_of(src_node, sw);
            if ((*this)(phase, port) == dst_node) {
                return offset;
            }
        }
    }
    assert(false);
    return -1;
}

void Topology::pushTopology(phase_t phase, const node_t* const targets) {
    std::copy(targets, targets + num_ports(), &topology[phase * num_ports()]);
}

void Topology::resizeLimits() {
    capacities.resize(num_nodes);
    bandwidths.resize(num_ports());
    topology.clear();
    topology.resize(num_phases * num_ports());
}

void extPushNetwork(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_switches,
                    const packet_t* node_capacities, const packet_t* port_bandwidth) {
    network.topology = Topology(num_phases, num_nodes, num_switches);
    network.topology.resizeLimits();
    network.flows.resize(num_flows);
    network.buffers = Buffers(num_nodes, num_flows);

    for (node_t node = 0; node < num_nodes; ++node) {
        network.topology.capacities[node] = node_capacities[node];
    }
    for (port_t port = 0; port < network.topology.num_ports(); ++port) {
        network.topology.bandwidths[port] = port_bandwidth[port];
    }
}

void extPushTopology(phase_t phase, const node_t* targets) {
    network.topology.pushTopology(phase, targets);
}

void extSchedulerInit() {
    network.buffers.fill(0);
    init_scheduler();
}

void extPushFlow(flow_t flow, node_t ingress, node_t egress) {
    network.flows[flow] = Flow{ingress, egress};
}

void extGetScheduleChoiceAll(phase_t phase, const packet_t* buffer_data, int32_t* schedule_choice_output) {
    network.buffers.pushAllBuffers(buffer_data);
    prepare_scheduler_choices();
    int i = 0;
    for (node_t node = 0; node < network.topology.num_nodes; ++node) {
        for (flow_t flow = 0; flow < network.num_flows(); ++flow) {
            for (int sw = -1; sw < network.topology.num_switches; ++sw) {
                schedule_choice_output[i] = scheduler_choice(node, flow, phase, sw);
                i++;
            }
        }
    }
}
