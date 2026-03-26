#pragma once

#include <cstdint>
#include <vector>

// These match the types used in UPPAALs C-like language.
using packet_t = int32_t;
using phase_t = int32_t;
using node_t = int32_t;
using flow_t = int32_t;
using switch_t = int32_t;
using port_t = int32_t;

struct ScheduleChoice {
    port_t port;   // Port to store incoming packets in
    phase_t phase; // Phase to store send them in.
};

struct Flow {
    node_t ingress;  // Node packets enter the network.
    node_t egress;   // Node they will egress at.
};

struct Topology {
    int32_t num_phases = 0;
    int32_t num_nodes = 0;
    int32_t num_switches = 0;
    // For the following two variables var,
    // var[i] refers to a port.
    std::vector<packet_t> capacities = {};
    std::vector<packet_t> bandwidths = {};

    // stores at
    // node_t = topology[phase * NUM_PORTS + port]
    std::vector<node_t> topology;

    Topology() = default;
    Topology(int32_t num_phases, int32_t num_nodes, int32_t num_switches)
    : num_phases(num_phases), num_nodes(num_nodes), num_switches(num_switches) {}

    [[nodiscard]] constexpr switch_t num_ports() const { return num_switches * num_nodes; }
    [[nodiscard]] constexpr port_t port_of(node_t node, switch_t sw) const { return node * num_switches + sw; }
    // Returns the node that owns the port.
    [[nodiscard]] constexpr node_t port_owner(port_t port) const { return port / num_switches; }

    // Returns the node (id) that is the target of the port in the given phase.
    node_t operator()(phase_t phase, port_t port) {
        return topology[phase * num_ports() + port];
    }

    port_t next_port_to(node_t src_node, node_t dst_node, phase_t current_phase);
    phase_t phase_offset_next_connection(node_t src_node, node_t dst_node, phase_t current_phase);

    // Internal use below.
    void pushTopology(phase_t phase, const node_t *const targets) {
        std::copy(targets, targets + num_ports(), &topology[phase * num_ports()]);
    }

    void resizeLimits() {
        capacities.resize(num_nodes);
        bandwidths.resize(num_ports());
        topology.clear();
        topology.resize(num_phases * num_ports());
    }
};

struct Buffers {

    Buffers() = default;
    Buffers(node_t n_nodes, flow_t n_flows) : nodes_(n_nodes), flows_(n_flows) {
        values_.resize(n_nodes * n_flows);
    }

    // Returns the number of packets buffered of a given flow for transmission
    // by some port in a given phase.
    int32_t &operator()(node_t node, flow_t flow) {
        return values_[node * flows_ + flow];
    };

    void pushBuffers(node_t node, const packet_t* data);
    void pushAllBuffers(const packet_t* data);

    void fill(packet_t value = 0) {
        std::fill(values_.begin(), values_.end(), value);
    }

    uint32_t get_buffer_hash() const {
        uint32_t seed = values_.size();
        for(auto y : values_) {
            uint32_t x = static_cast<uint32_t>(y);
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }

private:
    std::vector<int32_t> values_;
    int32_t nodes_ = 0;
    int32_t flows_ = 0;
};

struct Network {
    Topology topology;
    std::vector<Flow> flows;
    Buffers buffers;

    [[nodiscard]] flow_t num_flows() const { return flows.size(); }
};

extern Network network;

#ifdef __cplusplus
extern "C" {
// Core interface
void extPushNetwork(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_switches,
                    const packet_t* node_capacities, const packet_t* port_bandwidth);
void extPushTopology(phase_t phase, const node_t *targets);
void extPushFlow(flow_t flow, node_t ingress, node_t egress);
void extSchedulerInit(); // Called before each query. Calls scheduler_init()
void extGetScheduleChoiceAll(phase_t phase, const packet_t* buffer_data, int32_t* schedule_choice_output);
}
#endif

// Schedulers must implement:
// Called before each UPPAAL query is run.
void init_scheduler();

// Called once for each simulation step before calls to get_scheduler_choice is made.
void prepare_scheduler_choices();

// Called each simulation step, for all node, flow and switch combinations in the current phase.
// REQUIREMENT: Calls to get_scheduler_choice must be deterministic given the function parameters as well as the content of network
int32_t get_scheduler_choice(node_t node, flow_t flow, phase_t phase_i, switch_t sw);
