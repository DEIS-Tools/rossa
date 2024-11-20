#pragma once

#include <cstdint>
#include <vector>

// These match the types used in UPPAALs C-like language.
using packet_t = int32_t;
using phase_t = int32_t;
using node_t = int32_t;
using flow_t = int32_t;
using port_t = int32_t;

struct ScheduleChoice {
    port_t port;   // Port to store incoming packets in
    phase_t phase; // Phase to store send them in.
};

struct Flow {
    node_t ingress;  // Node packets enter the network.
    node_t egress;   // Node they will egress at.
    packet_t amount; // Number of packets entering each phase.
};

struct Parameters {
    int32_t num_phases = 0;
    int32_t num_nodes = 0;
    int32_t num_flows = 0;
    int32_t num_ports = 0;
    // For the following two variables var,
    // var[i] refers to a port.
    std::vector<packet_t> capacities = {};
    std::vector<packet_t> bandwidths = {};

    // Internal use.
    void resizeLimits() {
        capacities.resize(num_ports);
        bandwidths.resize(num_ports);
    }
};

struct Topology {
    int32_t num_phases = 0;
    int32_t num_ports = 0;
    int32_t num_nodes = 0;

    // Returns a reference to the node that is the target
    // of the port in the given phase.
    node_t &operator()(phase_t phase, port_t port) {
        return topology[phase * num_ports + port];
    }

    // Returns the node that owns the port.
    node_t owner(port_t port) {
        return portOwner[port];
    }

    // Internal use below.
    void pushTopology(phase_t phase, const node_t *const targets) {
        std::copy(targets, targets + num_ports, &topology[phase * num_ports]);
    }

    void pushOwners(const node_t *const owners) {
        std::copy(owners, owners + num_ports, portOwner.begin());
    }

    void resizeLimits() {
        topology.clear();
        topology.resize(num_phases * num_ports);
        portOwner.clear();
        portOwner.resize(num_ports);
    }

    // stores at
    // node_t = topology[phase * NUM_PORTS + port]
    std::vector<node_t> topology;
    std::vector<node_t> portOwner;
};

struct Buffers {

    Buffers() = default;
    Buffers(phase_t n_phases, port_t n_ports, flow_t n_flows) : phases_(n_phases), ports_(n_ports), flows_(n_flows) {
        values_.resize(n_phases * n_ports * n_flows);
    }

    // Returns the number of packets buffered of a given flow for transmission
    // by some port in a given phase.
    int32_t &operator()(phase_t phase, port_t port, flow_t flow) {
        return values_[phase * ports_ * flows_ + port * flows_ + flow];
    };

    void pushBuffers(phase_t phase, port_t port, packet_t *data);

    void fill(packet_t value = 0) {
        std::fill(values_.begin(), values_.end(), value);
    }

private:
    std::vector<int32_t> values_;
    int32_t phases_ = 0;
    int32_t ports_ = 0;
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
void extBasicParams(int32_t num_phases, int32_t num_nodes, int32_t num_flows, int32_t num_ports);
void extPortCapacities(int32_t *data);
void extPortBandwidths(int32_t *data);
void extPushPortOwners(node_t *owners);
void extPushFlow(int32_t i, node_t ingress, node_t egress, int32_t amount);
void extPushTopology(phase_t phase_i, node_t *targets);
void extPushBuffers(int32_t phase, port_t port, node_t *data);
void extSetup(); // Called after model construction in UPPAAL. Calls customSetup()
void extBegin(); // Called before each query. Calls customBegin()
void extPrepareChoices();
void extGetScheduleChoice(phase_t phase_i, node_t node, flow_t flow, phase_t &choice_phase, port_t &choice_port);

// Utilities
packet_t extGetPacketsInNetwork();

// Schedulers must implement:

// Called after UPPAAL model loaded (or changed).
// The call is made after the network parameters have been set.
void customSetup();

// Called before each UPPAAL query is run.
void customBegin();

// Called once for each simulation step before calls to customGetScheduleChoice is made.
void customPrepareChoices();

// Called (perhaps multiple times) each simulation step, and for all phase, node and flow combinations.
// Must write to the output references choice_phase and choice_port.
// REQUIREMENT: Between calls to customPrepareChoices this function must yield the same result for the same arguments
void customGetScheduleChoice(phase_t phase_i, node_t node, flow_t flow, phase_t &choice_phase, port_t &choice_port);
}
#endif

struct PortLoad {
    packet_t getPackets(port_t port, phase_t phase) const {
        packet_t total = 0;
        for (flow_t f = 0; f < network.parameters.num_flows; ++f) {
            total += network.buffers(phase, port, f);
        }
        return total;
    }

    // Returns the fraction of packets buffered at this port to be sent in the given
    // phase compared to its capacity.
    double getLoad(port_t port, phase_t phase) const {
        return static_cast<double>(getPackets(port, phase)) / network.parameters.capacities[port];
    }

    packet_t getTotalPackets(port_t port) const {
        packet_t total = 0;
        for (phase_t phase = 0; phase < network.parameters.num_phases; ++phase) {
            total += getPackets(port, phase);
        }
        return total;
    }

    // Returns the fraction of packets buffered at this port compared to its capacity.
    double getTotalPortLoad(port_t port) const {
        double total = 0;
        for (phase_t phase = 0; phase < network.parameters.num_phases; ++phase) {
            total += getLoad(port, phase);
        }
        return total;
    }
};