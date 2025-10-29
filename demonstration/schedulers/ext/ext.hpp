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

struct Parameters {
    int32_t num_phases = 0;
    int32_t num_nodes = 0;
    int32_t num_flows = 0;
    int32_t num_switches = 0;
    // For the following two variables var,
    // var[i] refers to a port.
    std::vector<packet_t> capacities = {};
    std::vector<packet_t> bandwidths = {};

    [[nodiscard]] constexpr switch_t num_ports() const { return num_switches * num_nodes; }
    [[nodiscard]] constexpr port_t port_of(node_t node, switch_t sw) const { return node * num_switches+ sw; }
    [[nodiscard]] constexpr node_t port_owner(port_t port) const { return port / num_switches; }

    // Internal use.
    void resizeLimits() {
        capacities.resize(num_nodes);
        bandwidths.resize(num_ports());
    }
};

struct Topology {
    int32_t num_phases = 0;
    int32_t num_switches = 0;
    int32_t num_nodes = 0;

    Topology() = default;
    explicit Topology(const Parameters& parameters)
    : num_phases(parameters.num_phases), num_switches(parameters.num_switches), num_nodes(parameters.num_nodes) {}

    [[nodiscard]] constexpr switch_t num_ports() const { return num_switches * num_nodes; }

    // Returns a reference to the node that is the target
    // of the port in the given phase.
    node_t &operator()(phase_t phase, port_t port) {
        return topology[phase * num_ports() + port];
    }

    // Returns the node that owns the port.
    node_t owner(port_t port) const {
        return port / num_switches;
    }
    constexpr port_t port_of(node_t node, switch_t sw) const {
        return node * num_switches + sw;
    }

    // Internal use below.
    void pushTopology(phase_t phase, const node_t *const targets) {
        std::copy(targets, targets + num_ports(), &topology[phase * num_ports()]);
    }

    // void pushOwners(const node_t *const owners) {
    //     std::copy(owners, owners + num_ports, portOwner.begin());
    // }

    void resizeLimits() {
        topology.clear();
        topology.resize(num_phases * num_ports());
        // portOwner.clear();
        // portOwner.resize(num_ports);
    }

    // stores at
    // node_t = topology[phase * NUM_PORTS + port]
    std::vector<node_t> topology;
    // std::vector<node_t> portOwner;
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

    void pushBuffers(node_t node, packet_t *data);

    void fill(packet_t value = 0) {
        std::fill(values_.begin(), values_.end(), value);
    }

private:
    std::vector<int32_t> values_;
    int32_t nodes_ = 0;
    int32_t flows_ = 0;
};

struct Network {
    Parameters parameters;
    Buffers buffers;
    std::vector<Flow> flows;
    Topology topology;
};

extern Network network;

#ifdef __cplusplus
extern "C" {
// Core interface
void extPushNetwork(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_switches,
                    const int32_t *node_capacities, const int32_t *port_bandwidth,
                    const node_t* flow_ingress, const node_t* flow_egress);
void extPushTopology(phase_t phase_i, const node_t *targets);
void extSchedulerInit(); // Called before each query. Calls scheduler_init()
void extPushBuffers(node_t node, packet_t *data);
void extPrepareChoices();
void extGetScheduleChoice(node_t node, flow_t flow, phase_t phase_i, switch_t sw, packet_t& choice_weight);
}
#endif

// Schedulers must implement:
// Called before each UPPAAL query is run.
// void customBegin();
void scheduler_init();

// Called once for each simulation step before calls to customGetScheduleChoice is made.
void customPrepareChoices();

// Called (perhaps multiple times) each simulation step, and for all phase, node and flow combinations.
// Must write to the output references choice_phase and choice_port.
// REQUIREMENT: Between calls to customPrepareChoices this function must yield the same result for the same arguments
void customGetScheduleChoice(node_t node, flow_t flow, phase_t phase_i, switch_t sw, packet_t& choice_weight);

struct PortLoad {

    static packet_t getPacketsForFlow(node_t node, flow_t flow) {
        return network.buffers(node, flow);
    }

    static packet_t getPackets(node_t node) {
        packet_t total = 0;
        for (flow_t f = 0; f < network.parameters.num_flows; ++f) {
            total += network.buffers(node, f);
        }
        return total;
    }
    static packet_t getTotalPortLoad(port_t port) {
        // FIXME: In the new model, the notion of port load does not exists. The node has a single queue.
        //        For now we pretend node load is uniformly distributed across the "ports", so the capacity-based schedulers can still compile.
        return getLoad(network.topology.owner(port)) / network.parameters.num_switches;
    }

    // Returns the fraction of packets buffered at this node compared to its capacity.
    static double getLoad(node_t node) {
        return static_cast<double>(getPackets(node)) / network.parameters.capacities[node];
    }
};