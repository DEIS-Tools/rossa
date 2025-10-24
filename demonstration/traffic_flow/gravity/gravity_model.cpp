#include "traffic.hpp"
#include "util.h"

#include <unordered_map>

namespace views = std::views;
using namespace rossa::traffic;

std::mt19937 random_gen_masses;
std::mt19937 random_gen_flows{std::random_device{}()};

std::vector<double> node_send_mass;
std::vector<double> node_recv_mass;
struct hash_pair final {
    template<class TFirst, class TSecond>
    size_t operator()(const std::pair<TFirst, TSecond>& p) const noexcept {
        uintmax_t hash = std::hash<TFirst>{}(p.first);
        hash <<= sizeof(uintmax_t) * 4;
        hash ^= std::hash<TSecond>{}(p.second);
        return std::hash<uintmax_t>{}(hash);
    }
};
std::vector<Flow> flows;
// std::unordered_map<std::pair<node_t,node_t>, packet_t, hash_pair> flows;
std::unordered_map<std::pair<node_t,node_t>, packet_t, hash_pair> modified_flows;


// Called after UPPAAL model loaded (or changed).
// The call is made after the parameters have been set.
void customTrafficSetup() {
    // TODO: Gen seed from ENV
    random_gen_masses = std::mt19937(123456);
    random_fill(node_send_mass, parameters.num_nodes, random_gen_masses);
    random_fill(node_recv_mass, parameters.num_nodes, random_gen_masses);

    // TODO: Get parameters from ENV!
    double min_demand = 1;
    double max_demand = 300;
    double power = 2.0;
    int connection_percentage = 25;

    double delta = max_demand - min_demand;

    auto num_active_flows = static_cast<size_t>(round(count_partial_permutations(parameters.num_nodes, 2) * connection_percentage * 0.01));
    auto send_recv_pairs = sample_partial_permutations<2>(views::iota(0, parameters.num_nodes), num_active_flows, random_gen_masses);
    flows.clear();
    flows.reserve(send_recv_pairs.size());
    for (const auto& [ingress, egress] : send_recv_pairs) {
        auto amount = round(min_demand + std::pow(node_send_mass[ingress], power) * std::pow(node_recv_mass[egress], power) * delta);
        flows.emplace_back(ingress, egress, amount);
        // flows.emplace(std::make_pair(ingress, egress), amount);
    }
}

// Called before each UPPAAL query is run.
void customTrafficBegin() {
    // TODO: Get seed from ENV??
    // random_gen_flows = std::mt19937(std::random_device{}());
}

template<typename G>
double random(const double max, G&& g) {
    return std::uniform_real_distribution<>{0, max}(std::forward<G>(g));
}
// Called once for each simulation step before calls to customTrafficFlow is made.
void customTrafficPrepareChoices() {
    modified_flows.clear();
    modified_flows.reserve(flows.size());
    for (const auto& [ingress, egress, amount] : flows) {
        const double fAmount = fmax(0.0, amount * (1.0 + random(0.4, random_gen_flows) - 0.2));
        modified_flows[std::make_pair(ingress, egress)] = static_cast<packet_t>(round(fAmount));
    }
}

void customTrafficGetFlow(int timestep, node_t ingress, node_t egress, packet_t& amount) {
    (void)timestep;
    const auto it = modified_flows.find(std::make_pair(ingress, egress));
    amount = it == modified_flows.end() ? 0 : it->second;
}
