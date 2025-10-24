#include "traffic.hpp"

namespace rossa::traffic {
    Parameters parameters;
}

using namespace rossa::traffic;

void trafficBasicParams(node_t num_nodes) {
    parameters = Parameters{num_nodes};
}

void trafficBegin() {
    customTrafficBegin();
}

void trafficSetup() {
    customTrafficSetup();
}

void trafficPrepareChoices() {
    customTrafficPrepareChoices();
}

void trafficGetFlow(int timestep, node_t ingress, node_t egress, packet_t& amount) {
    customTrafficGetFlow(timestep, ingress, egress, amount);
}
