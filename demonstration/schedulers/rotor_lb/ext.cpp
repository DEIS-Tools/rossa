#include "ext.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>
#include <ranges>
namespace views = std::views;

/*
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/reverse_graph.hpp>

#include "temporal_graph.hpp"


enum APPROACH { quickest, fewest_hops };
struct Params {
    APPROACH approach = quickest;
};
Params params{quickest};
*/

struct ChoiceArgs {
    int32_t phase;
    int32_t node;
    // int32_t destination;
    int32_t flow;
    bool operator==(const ChoiceArgs &other) const {
        return phase == other.phase && node == other.node && flow == other.flow;
    }
    bool operator!=(const ChoiceArgs &other) const {
        return !(*this == other);
    }
};

template <>
struct std::hash<ChoiceArgs> {
    std::size_t operator()(const ChoiceArgs &args) const noexcept {
        return std::hash<int32_t>{}((args.phase << 24) + (args.node << 8) + args.flow);
    }
};

std::mt19937 random_gen;
// Random number chosen for this simulation step.
uint32_t random_num;

// std::unique_ptr<tg::TemporalGraph> tgGraph;
std::unique_ptr<std::unordered_map<ChoiceArgs, ScheduleChoice>> pChoiceCache;


// From: https://arxiv.org/abs/1504.06804
// hashes x strongly universally into the range [m]
uint32_t hash_bounded(uint32_t x, uint32_t m) {
    constexpr uint64_t a = 0x28ec0f222c79fb46; // uniformly selected
    constexpr uint64_t b = 0x2179c594b7d54ca2; // uniformly selected
    return (((a * x + b) >> 32) * m) >> 32;
}

/*
template <class P>
std::vector<tg::TVertex> outNeighbours(tg::Graph::vertex_descriptor from, P pred) {
    std::vector<tg::TVertex> result;
    for (auto [adj, adjEnd] = boost::adjacent_vertices(from, tgGraph->graph);
         adj != adjEnd; ++adj) {
        const auto &vertex = tgGraph->graph[*adj];
        if (pred(vertex)) {
            result.push_back(vertex);
        }
    }
    return result;
}

void computeToDestination(node_t destination) {
    using namespace boost;

    auto destVertex = tgGraph->vNodes[destination];
    // We reverse the graph to find all solutions to this node.
    auto g = make_reverse_graph(tgGraph->graph);

    std::vector<tg::Graph::vertex_descriptor> p(num_vertices(g));
    std::vector<int> d(num_vertices(g));

    const auto approach = params.approach;
    auto wmap = make_transform_value_property_map(
        [approach](const tg::TEdge &edge) {
            if (approach == fewest_hops) {
                return 10'000 * edge.hop + edge.time;
            }
            return 10'000 * edge.time + edge.hop; 
        },
        get(edge_bundle, g));

    dijkstra_shortest_paths(g, destVertex,
        weight_map(wmap)
            .predecessor_map(make_iterator_property_map(p.begin(), get(vertex_index, g)))
            .distance_map(make_iterator_property_map(d.begin(), get(vertex_index, g))));

    auto const& params = network.parameters;

    for (phase_t i=0; i < params.num_phases; ++i) {
        for (node_t from_node=0; from_node < params.num_nodes; ++from_node) {
            port_t port = findOwnedPort(tgGraph->topology, from_node);
            phase_t phase = tgGraph->phaseAdd(i, 1);

            auto currentVertex = tgGraph->vPN[tgGraph->pnIndex(i, from_node)];
            auto next = p[currentVertex];
            // Keep skipping phasenode until the hop.
            assert(!std::holds_alternative<tg::TPhaseNode>(g[next]));
            for (; std::holds_alternative<tg::TPhaseNode>(g[next]); next = p[next]) {
                assert(false); // Since we changed the temporal graph this should not be
                            // the case.
            }
            if (const auto *pNext = std::get_if<tg::TPort>(&g[next])) {
                port = pNext->port;
                phase = pNext->phase;
            }
            // Store for all flows that have that egress node.
            (*pChoiceCache)[{i, from_node, destination}] = {port, phase};
        }
    }
}*/

auto owned_ports(const node_t node) {
    return views::iota(0, network.parameters.num_ports) | views::filter([node](const port_t port){ return network.topology.owner(port) == node; });
}

class RotorLbTable {
public:
    RotorLbTable(node_t n_nodes, node_t local) : n_nodes_(n_nodes), table_(n_nodes * n_nodes_), local_(local) {}

    packet_t& traffic(node_t source, node_t destination) {
        return table_[source * n_nodes_ + destination];
    }
    [[nodiscard]] const packet_t& traffic(node_t source, node_t destination) const {
        return table_[source * n_nodes_ + destination];
    }
    packet_t& operator()(flow_t flow) { return traffic(network.flows[flow].ingress, network.flows[flow].egress); }
    packet_t& operator()(node_t source, node_t destination) { return traffic(source, destination); }
    [[nodiscard]] const packet_t& operator()(node_t source, node_t destination) const { return traffic(source, destination); }

    [[nodiscard]] packet_t local_traffic(node_t destination) const {
        return (*this)(local_, destination);
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
        : offer(parent.n_nodes_), capacity(network.parameters.bandwidths[port]), target(target) {}
        std::vector<packet_t> offer;
        packet_t capacity;
        node_t target;
    };
    std::vector<Offer> get_offer() {
        std::vector<Offer> offers;
        for (const auto& [target, port] : targets()) {
            auto& offer = offers.emplace_back(*this, port, target);
            // First send direct non-local traffic
            for (const auto node : non_local()) {
                offer.capacity -= traffic(node, target);
                traffic(node, target) = 0;
            }
            // Next send direct local traffic
            offer.capacity -= local_traffic(target);
        }
        for (const auto node : non_local()) {
            if (!is_target(node)) {
                // Offer remaining local traffic to all targets (Algorithm in RotorNet2017 paper does not specify the case of multiple targets...)
                for (auto& offer : offers) {
                    offer.offer[node] = local_traffic(node);
                }
            }
        }
        return offers;
    }

    [[nodiscard]] port_t get_choice(flow_t flow) const {
        return get_choice(network.flows[flow].ingress, network.flows[flow].egress);
    }
    [[nodiscard]] port_t get_choice(node_t source, node_t destination) const {
        for (const auto& [target, port] : targets()) {
            if (target == destination) { // Direct traffic
                if (source != local_) {
                    return port; // 1st priority: Non-local direct traffic
                }
                return port; // 2nd priority: Local direct traffic. TODO: if no more capacity, return -1;
            }
        }
        if (source == local_) {
            // TODO: 3rd priority: Local indirect traffic, if offer was accepted by target.
            for (const auto& [target, port] : targets()) {
                // ...
            }
        }
        // TODO: Simulation must allow not immediately scheduling for future ports, but wait and see which port is good to choose.
        // return -1; // Don't schedule yet.
        return targets()[random_num%targets().size()].second;  // Random target for now...
    }

private:
    node_t n_nodes_ = 0;
    std::vector<packet_t> table_;
    node_t local_ = 0;
    std::vector<std::pair<node_t,port_t>> targets_;  // (node,port) \in targets: In current phase, we can send traffic to node through port.
};

void compute_rotor_lb(phase_t phase_i) {
    auto const& params = network.parameters;

    std::vector<RotorLbTable> tables;
    std::vector<std::vector<RotorLbTable::Offer>> offers;
    tables.reserve(params.num_nodes);
    // Build tables from port load data
    for (const node_t node : views::iota(0, params.num_nodes)) {
        auto& table = tables.emplace_back(params.num_nodes, node);
        for (const port_t owned_port : owned_ports(node)) {
            node_t target = network.topology(phase_i, owned_port);
            table.add_target(target, owned_port);
            for (const flow_t flow : views::iota(0, params.num_flows)) {
                packet_t load = PortLoad::getPacketsForFlow(owned_port, phase_i, flow);
                table(flow) = load;
            }
        }
        offers.emplace_back(table.get_offer());
    }



    for (const node_t node : views::iota(0, params.num_nodes)) {
        for (const flow_t flow : views::iota(0, params.num_flows)) {
            // TODO: Make choice based on RotorLB algorithm
            port_t choice = tables[node].get_choice(flow);
            (*pChoiceCache)[{phase_i, node, flow}] = {choice, phase_i};
        }
    }
}

static ScheduleChoice cachedChoice(const phase_t phase_i, const node_t node, const flow_t flow) {
    auto key = ChoiceArgs{phase_i, node, flow};
    auto iter = pChoiceCache->find(key);
    if (iter == pChoiceCache->end()) {
        compute_rotor_lb(phase_i);
        iter = pChoiceCache->find(key);
    }
    return iter->second;
}

// local data per destination
// non-local data per source and destination
// = table per node of traffic enqueued per (source,destination)-pair except diagonal and self-destination.

void customGetScheduleChoice(phase_t phase_i, node_t node, flow_t flow, phase_t &choice_phase, port_t &choice_port) {
    auto choice = cachedChoice(phase_i, node, flow);
    choice_phase = choice.phase;
    choice_port = choice.port;
}

void customPrepareChoices() {
    random_num = random_gen();
}

void customSetup() {
    // readEnvVars();
    // tgGraph = std::make_unique<tg::TemporalGraph>(network.topology);
    pChoiceCache = std::make_unique<std::unordered_map<ChoiceArgs, ScheduleChoice>>();
    // constructSolutions();
}

void customBegin() {
    random_gen = std::mt19937(123456);
}
