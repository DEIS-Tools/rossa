#include "ext.hpp"

#include <cassert>
#include <algorithm>
#include <vector>

Network network;

void Buffers::pushBuffers(node_t node, const packet_t *data) {
    const size_t start = node * flows_;
    std::copy_n(data, flows_, &values_[start]);
}
void Buffers::pushAllBuffers(const packet_t *data) {
    std::copy_n(data, nodes_ * flows_, &values_[0]);
}

port_t Topology::next_port_to(node_t src_node, node_t dst_node, phase_t current_phase) {
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
phase_t Topology::phase_offset_next_connection(node_t src_node, node_t dst_node, phase_t current_phase) {
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
                schedule_choice_output[i] = get_scheduler_choice(node, flow, phase, sw);
                i++;
            }
        }
    }
}
