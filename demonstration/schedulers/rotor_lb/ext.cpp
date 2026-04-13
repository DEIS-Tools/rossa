#include "ext.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>
#include <ranges>
namespace views = std::views;

#include <boost/container_hash/hash.hpp>

enum APPROACH { uniform, quickest };
struct Params {
    APPROACH approach = uniform;
};
Params params{uniform};

class EnvVarException : public std::exception {
public:
    virtual const char *what() const noexcept { return "Bad ENV var set"; }
};
void readEnvVars() {
    if (const auto *envVal = std::getenv("CHOICE_APPROACH")) {
        if (std::strcmp(envVal, "QUICKEST") == 0) {
            params.approach = quickest;
        } else if (std::strcmp(envVal, "UNIFORM") == 0) {
            params.approach = uniform;
        } else {
            throw EnvVarException{};
        }
    }
}

struct ChoiceArgs {
    node_t node;
    flow_t flow;

    [[nodiscard]] auto as_tuple() const -> decltype(auto) {
        return std::tie(node, flow);
    }
    bool operator==(const ChoiceArgs &other) const {
        return as_tuple() == other.as_tuple();
    }
    bool operator!=(const ChoiceArgs &other) const {
        return !(*this == other);
    }
};
std::size_t hash_value(const ChoiceArgs& v) {
    return boost::hash_value(v.as_tuple());
}
namespace std {
    template<> struct hash<::ChoiceArgs> : boost::hash<::ChoiceArgs> {};
}

struct PortWeight {
    PortWeight(port_t port, packet_t weight) : port(port), weight(weight) {}
    explicit PortWeight(port_t port) : port(port), weight(1) {}
    port_t port;
    packet_t weight;
};
struct SchedulerChoice : std::vector<PortWeight> {
    SchedulerChoice() = default;
    SchedulerChoice(std::initializer_list<PortWeight> port_weights) : std::vector<PortWeight>{port_weights} {}
    explicit SchedulerChoice(PortWeight port_weight) : std::vector<PortWeight>{port_weight} {}
    explicit SchedulerChoice(port_t port) : std::vector<PortWeight>{PortWeight(port)} {}
    explicit SchedulerChoice(port_t port, packet_t weight) : std::vector<PortWeight>{PortWeight(port, weight)} {}
};


// std::mt19937 random_gen;
// Random number chosen for this simulation step.
// uint32_t random_num;

std::unique_ptr<std::unordered_map<ChoiceArgs, SchedulerChoice>> pChoiceCache = nullptr;


inline void fairshare_1d(std::vector<packet_t>& v, packet_t capacity) {
    std::vector<packet_t> input = v;
    for (auto& e : v) e = 0;
    while(true) {
        packet_t count_none_zero = std::ranges::count_if(input, [](const auto& e){ return e > 0; });
        if (count_none_zero == 0) break;
        packet_t fair_share = capacity / count_none_zero;
        if (fair_share == 0) break;
        for (const auto i : views::iota(static_cast<decltype(input.size())>(0), input.size())) {
            if (input[i] >= fair_share) {
                input[i] -= fair_share;
                v[i] += fair_share;
                capacity -= fair_share;
            } else if (input[i] > 0) {
                capacity -= input[i];
                v[i] += input[i];
                input[i] = 0;
            }
        }
    }
    if (capacity > 0) {
        for (const auto i : views::iota(static_cast<decltype(input.size())>(0), input.size())) {
            if (input[i] > 0) {
                input[i]--;
                v[i]++;
                capacity--;
            }
            if (capacity == 0) break;
        }
    }
}

class RotorLbTable {
public:
    RotorLbTable(node_t n_nodes, node_t local) : n_nodes_(n_nodes), table_(n_nodes_ * n_nodes_), direct_traffic_(n_nodes_ * n_nodes_), local_(local) {}

    packet_t& traffic(node_t source, node_t destination) {
        return table_[source * n_nodes_ + destination];
    }
    [[nodiscard]] const packet_t& traffic(node_t source, node_t destination) const {
        return table_[source * n_nodes_ + destination];
    }
    packet_t& operator()(flow_t flow) { return traffic(network.flows[flow].ingress, network.flows[flow].egress); }
    [[nodiscard]] const packet_t& operator()(flow_t flow) const { return traffic(network.flows[flow].ingress, network.flows[flow].egress); }
    packet_t& operator()(node_t source, node_t destination) { return traffic(source, destination); }
    [[nodiscard]] const packet_t& operator()(node_t source, node_t destination) const { return traffic(source, destination); }

    [[nodiscard]] packet_t& local_traffic(node_t destination) {
        return (*this)(local_, destination);
    }
    [[nodiscard]] const packet_t& local_traffic(node_t destination) const {
        return (*this)(local_, destination);
    }
    packet_t& direct_traffic(node_t source, node_t destination) {
        return direct_traffic_[source * n_nodes_ + destination];
    }
    const packet_t& direct_traffic(node_t source, node_t destination) const {
        return direct_traffic_[source * n_nodes_ + destination];
    }
    [[nodiscard]] auto non_local() const {
        return views::iota(0, n_nodes_)
             | views::filter([this](node_t node){ return node != local_; });
    }
    [[nodiscard]] auto non_local_traffic(node_t destination) const {
        return non_local() | views::transform([destination, this](node_t node){ return std::make_pair(node, traffic(node, destination)); });
    }
    void add_target(node_t node, port_t port) {
        targets_.emplace_back(node, port);
    }
    [[nodiscard]] const std::vector<std::pair<node_t,port_t>>& targets() const {
        return targets_;
    }
    [[nodiscard]] bool is_target(node_t node) const {
        return std::ranges::any_of(targets() | views::keys, [node](auto&& target){ return target == node; });
    }
    struct Offer {
        Offer(const RotorLbTable& parent, port_t port, node_t target)
        : offer(parent.n_nodes_), capacity(network.topology.bandwidths[port]), source(parent.local_), target(target) {}
        std::vector<packet_t> offer;
        packet_t capacity;
        node_t source;
        node_t target;
    };
    std::vector<Offer> get_offer() {
        std::vector<Offer> offers;
        for (const auto& [target, port] : targets()) {
            auto& offer = offers.emplace_back(*this, port, target);
            // Prioritize direct non-local traffic
            for (const auto node : non_local()) {
                direct_traffic(node, target) = traffic(node, target);
                traffic(node, target) = 0;
                offer.capacity -= direct_traffic(node, target);
                assert(offer.capacity >= 0);
            }
            // Next prioritize direct local traffic (use as much as possible)
            if (offer.capacity < local_traffic(target)) {
                direct_traffic(local_, target) = offer.capacity;
                local_traffic(target) -= offer.capacity;
            } else {
                direct_traffic(local_, target) = local_traffic(target);
                local_traffic(target) = 0;
            }
            offer.capacity -= direct_traffic(local_, target);
            assert(offer.capacity >= 0);
        }
        for (auto& offer : offers) {
            for (const auto node : non_local()) {
                if (!is_target(node)) {
                    // Offer remaining local traffic to all targets (Algorithm in RotorNet2017 paper does not specify the case of multiple targets...)
                    offer.offer[node] = local_traffic(node);
                }
            }
        }
        return offers;
    }

    void accept_offers(std::vector<std::vector<Offer>>& offers, const phase_t phase_i) {
        // Find offers to local
        std::vector<std::reference_wrapper<Offer>> offers_to_local;  // Could work, as indirect representation of matrix.
        for (auto& offer_vector : offers) {
            for (auto& offer : offer_vector) {
                if (offer.target == local_) {
                    offers_to_local.emplace_back(offer);
                    assert(offer.offer[local_] == 0);  // Since this is about indirect traffic, we only use non_local destinations. We verify that here.
                }
            }
        }

        // For each destination, find how much traffic can be accepted
        std::vector<packet_t> destination_capacity(n_nodes_);
        for (const node_t destination : non_local()) {
            packet_t remaining_traffic = 0;
            for (const node_t source : views::iota(0, n_nodes_)) {
                remaining_traffic += traffic(source, destination);
            }
            packet_t available = network.topology.bandwidths[network.topology.next_port_to(local_, destination, phase_i)] - remaining_traffic;
            destination_capacity[destination] = available >= 0 ? available : 0;
        }

        // Fairshare offers among available buffer capacity and link capacity.
        std::vector<std::vector<packet_t>> input(n_nodes_);
        for (const auto offer : offers_to_local) {
            input[offer.get().source] = offer.get().offer;  // Copy offer to input matrix
        }
        for (auto& offer : offers_to_local) for (auto& e : offer.get().offer) e = 0;  // Reset, so we can build it up

        do {
            std::vector<std::vector<packet_t>> offer_matrix(n_nodes_);  // (offer.source) x (traffic destination)
            for (const auto& offer : offers_to_local) {
                offer_matrix[offer.get().source] = input[offer.get().source];  // Copy offer and calculate fairshare over link capacity
                fairshare_1d(offer_matrix[offer.get().source], offer.get().capacity);
            }
            for (const node_t destination : non_local()) {
                std::vector<packet_t> destination_offers(n_nodes_);
                for (const auto& offer : offers_to_local) {
                    destination_offers[offer.get().source] = offer_matrix[offer.get().source][destination];
                }
                fairshare_1d(destination_offers, destination_capacity[destination]);
                for (auto& offer : offers_to_local) {
                    // Update accepted offer
                    offer.get().offer[destination] += destination_offers[offer.get().source];
                    input[offer.get().source][destination] -= destination_offers[offer.get().source];
                    // Update capacity info
                    destination_capacity[destination] -= destination_offers[offer.get().source];
                    offer.get().capacity -= destination_offers[offer.get().source];
                }
            }
            // Clear rows with no more capacity
            for (const auto& offer : offers_to_local) {
                assert(offer.get().capacity >= 0);
                if (offer.get().capacity == 0) {
                    for (const node_t destination : non_local()) {
                        input[offer.get().source][destination] = 0;
                    }
                }
            }
            // Clear columns with no more capacity
            for (const node_t destination : non_local()) {
                assert(destination_capacity[destination] >= 0);
                if (destination_capacity[destination] == 0) {
                    for (const auto& offer : offers_to_local) {
                        input[offer.get().source][destination] = 0;
                    }
                }
            }
        } while (std::ranges::any_of(input, [](auto&& v){ return std::ranges::any_of(v, [](auto&& e){ return e > 0; }); }));
    }

    [[nodiscard]] SchedulerChoice get_choice(flow_t flow, const std::vector<std::vector<Offer>>& offers, phase_t phase_i) const {
        node_t source = network.flows[flow].ingress;
        node_t destination = network.flows[flow].egress;
        SchedulerChoice scheduler_choice;
        // Check if this flow can be sent as direct traffic to destination (in this phase)
        for (const auto& [target, port] : targets()) {
            if (target == destination) {
                // 1st and 2nd priority: Direct traffic (local and non-local)
                scheduler_choice.emplace_back(port, direct_traffic(source, target));
                if (traffic(source, target) > 0) {  // Any remaining traffic in buffer after sending direct_traffic(source, target)?
                    // Only sending some of the buffered flow, so adding a dummy port for the rest.
                    scheduler_choice.emplace_back(-1, traffic(source, target));
                }
                return scheduler_choice;
            }
        }
        // If we get here, the flow is indirect (destination is not a target).
        // Check if flow is local, else ignore in this phase.
        if (source == local_) {
            // 3rd priority: Sending local indirect traffic, based on how much was accepted by target
            std::vector<std::pair<PortWeight,phase_t>> options;
            for (const auto& [target, port] : targets()) {
                assert(target != destination);
                auto it = std::ranges::find(offers[local_], target, [](const auto& offer){ return offer.target; });
                if (it != std::ranges::end(offers[local_]) && it->offer[destination] > 0) {
                    auto priority = params.approach == uniform ? 0 : network.topology.phase_offset_next_connection(target, destination, phase_i);
                    options.emplace_back(PortWeight(port, it->offer[destination]), priority);
                }
            }

            // If the targets in total accept more traffic than we have, prioritize sending to targets that sooner has connection to the destination.
            std::ranges::sort(options, std::less<phase_t>(), [](const auto& e){ return e.second; });
            packet_t buffered = network.buffers(source, flow);
            phase_t last_offset = -1;
            std::vector<PortWeight> options_with_same_offset;
            auto handle_equal_priority_options = [&buffered, &scheduler_choice](const std::vector<PortWeight>& equal_priority_options) -> bool {
                auto choice_weights = equal_priority_options | views::transform([](const auto& c){ return c.weight; }) | std::ranges::to<std::vector>();
                if (const auto sum = std::ranges::fold_left(choice_weights, 0, std::plus<packet_t>()); buffered >= sum) {
                    buffered -= sum;
                    for (const auto& choice : equal_priority_options) {
                        scheduler_choice.emplace_back(choice);
                    }
                } else {
                    fairshare_1d(choice_weights, buffered);
                    for (const auto& [choice, weight] : std::views::zip(equal_priority_options, choice_weights)) {
                        scheduler_choice.emplace_back(choice.port, weight);
                    }
                    buffered = 0;
                    return true;
                }
                return false;
            };
            int count = 0;
            for (const auto& [option, offset] : options) {
                if (offset != last_offset) {
                    count++;
                    if (!options_with_same_offset.empty()) {
                        if (handle_equal_priority_options(options_with_same_offset)) break;
                    }
                    last_offset = offset;
                    options_with_same_offset.clear();
                }
                options_with_same_offset.emplace_back(option);
            }
            if (buffered > 0) {
                handle_equal_priority_options(options_with_same_offset);
            }
            if (buffered > 0) {
                // Any remaining traffic in buffer will use the dummy port
                scheduler_choice.emplace_back(-1, buffered);
            }
        }
        return scheduler_choice;
    }

private:
    node_t n_nodes_ = 0;
    std::vector<packet_t> table_;
    std::vector<packet_t> direct_traffic_;
    node_t local_ = 0;
    std::vector<std::pair<node_t,port_t>> targets_;  // (node,port) \in targets: In current phase, we can send traffic to node through port.
};

void compute_rotor_lb(phase_t phase_i) {
    std::vector<RotorLbTable> tables;
    std::vector<std::vector<RotorLbTable::Offer>> offers;
    tables.reserve(network.topology.num_nodes);
    // Build tables from port load data
    for (const node_t node : views::iota(0, network.topology.num_nodes)) {
        auto& table = tables.emplace_back(network.topology.num_nodes, node);
        for (const switch_t sw : views::iota(0, network.topology.num_switches)) {
            port_t port = network.topology.port_of(node, sw);
            node_t target = network.topology(phase_i, port);
            table.add_target(target, port);
            for (const flow_t flow : views::iota(0, network.num_flows())) {
                table(flow) = network.buffers(node, flow);
            }
        }
        offers.emplace_back(table.get_offer());
    }
    // Accept offers
    for (auto& table : tables) {
        table.accept_offers(offers, phase_i);
    }
    // Convert accepted offers to scheduling choices
    for (const node_t node : views::iota(0, network.topology.num_nodes)) {
        for (const flow_t flow : views::iota(0, network.num_flows())) {
            (*pChoiceCache)[{node, flow}] = tables[node].get_choice(flow, offers, phase_i);
        }
    }
}

// local data per destination
// non-local data per source and destination
// = table per node of traffic enqueued per (source,destination)-pair except diagonal and self-destination.

packet_t scheduler_choice(node_t node, flow_t flow, phase_t phase_i, switch_t sw) {
    auto key = ChoiceArgs{node, flow};
    auto iter = pChoiceCache->find(key);
    if (iter == pChoiceCache->end()) {
        compute_rotor_lb(phase_i);
        iter = pChoiceCache->find(key);
    }
    auto& choice = iter->second;
    const port_t port = sw == -1 ? -1 : network.topology.port_of(node, sw);
    auto port_choice = std::ranges::find(choice, port, [](const auto& pw){ return pw.port; });
    return port_choice == choice.end() ? 0 : port_choice->weight;
}

void prepare_scheduler_choices() {
    pChoiceCache->clear();
}
void init_scheduler() {
    if (!pChoiceCache) {
        readEnvVars();
        pChoiceCache = std::make_unique<std::unordered_map<ChoiceArgs, SchedulerChoice>>();
    }
}
