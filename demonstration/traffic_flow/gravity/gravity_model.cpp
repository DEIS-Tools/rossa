
#include "traffic.hpp"
#include "util.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

namespace views = std::views;
using namespace rossa::traffic;

std::mt19937 random_gen_masses;
const auto flow_seed = std::random_device{}();
std::mt19937 random_gen_flows;

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
std::unique_ptr<std::vector<Flow>> flows;
std::unique_ptr<std::vector<packet_t>> base_demands;

double min_demand = 1;
double max_demand = 300;
double power = 2.0;
double connection_ratio = 0.25;
double demand_variance = 0.2;

void readEnvVars() {
    if (const auto *envVal = std::getenv("ROSSA_MIN_DEMAND")) {
        min_demand = std::stod(envVal);
    }
    if (const auto *envVal = std::getenv("ROSSA_MAX_DEMAND")) {
        max_demand = std::stod(envVal);
    }
    if (const auto *envVal = std::getenv("ROSSA_GRAVITY_MODEL_POWER")) {
        power = std::stod(envVal);
    }
    if (const auto *envVal = std::getenv("ROSSA_CONNECTION_RATIO")) {
        connection_ratio = std::stod(envVal);
    }
    if (const auto *envVal = std::getenv("ROSSA_DEMAND_VARIANCE")) {
        demand_variance = std::stod(envVal);
    }
}

// Called before each UPPAAL query is run.
const std::vector<Flow>& custom_init_flows() {
    if (!flows) {
        flows = std::make_unique<std::vector<Flow>>();
        base_demands = std::make_unique<std::vector<packet_t>>();

        readEnvVars();

        // TODO: Gen seed from ENV
        random_gen_masses = std::mt19937(123456);
        random_fill(node_send_mass, parameters.num_nodes, random_gen_masses);
        random_fill(node_recv_mass, parameters.num_nodes, random_gen_masses);

        double delta = max_demand - min_demand;

        auto num_active_flows = static_cast<size_t>(round(count_partial_permutations(parameters.num_nodes, 2) * connection_ratio));
        auto send_recv_pairs = sample_partial_permutations<2>(views::iota(0, parameters.num_nodes), num_active_flows, random_gen_masses);
        flows->clear();
        flows->reserve(send_recv_pairs.size());
        for (const auto& [ingress, egress] : send_recv_pairs) {
            auto amount = round(min_demand + std::pow(node_send_mass[ingress], power) * std::pow(node_recv_mass[egress], power) * delta);
            flows->emplace_back(ingress, egress);
            base_demands->emplace_back(amount);
        }
        assert(flows->size() == base_demands->size());
    }
    // TODO: Get seed from ENV??
    random_gen_flows = std::mt19937(flow_seed);
    return *flows;
}

template<typename G>
double random(const double max, G&& g) {
    return std::uniform_real_distribution<>{0, max}(std::forward<G>(g));
}

packet_t custom_get_flow_demand(flow_t flow, int timestep) {
    (void)timestep;
    if (flow >= static_cast<flow_t>(base_demands->size())) return 0;
    const double fAmount = fmax(0.0, base_demands->at(flow) * (1.0 + random(demand_variance * 2, random_gen_flows) - demand_variance));
    return static_cast<packet_t>(round(fAmount));
}
