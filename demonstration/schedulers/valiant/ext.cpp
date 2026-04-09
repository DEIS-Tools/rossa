#include "ext.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/reverse_graph.hpp>

#include "temporal_graph.hpp"


struct ChoiceArgs {
    int32_t phase;
    int32_t node;
    int32_t destination;
    //int32_t flow;
    bool operator==(const ChoiceArgs &other) const {
        return phase == other.phase && node == other.node && destination == other.destination;
    }
    bool operator!=(const ChoiceArgs &other) const {
        return !(*this == other);
    }
};

template <>
struct std::hash<ChoiceArgs> {
    std::size_t operator()(const ChoiceArgs &args) const noexcept {
        return (args.phase << 24) + (args.node << 8) + args.destination;
    }
};

std::mt19937 random_gen = std::mt19937(0);
// Random number for this whole simulation.
uint32_t random_num_simulation;
// Random number chosen for this simulation step.
uint32_t random_num;

std::unique_ptr<tg::TemporalGraph> tgGraph = nullptr;
std::unique_ptr<std::unordered_map<ChoiceArgs, ScheduleChoice>> pChoiceCache;


// From: https://arxiv.org/abs/1504.06804
// hashes x strongly universally into the range [m]
uint32_t hash_bounded(uint32_t x, uint32_t m) {
    constexpr uint64_t a = 0x28ec0f222c79fb46; // uniformly selected
    constexpr uint64_t b = 0x2179c594b7d54ca2; // uniformly selected
    return (((a * x + b) >> 32) * m) >> 32;
}

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

    auto wmap = make_transform_value_property_map(
        [](const tg::TEdge &edge) {
            return 10'000 * edge.time + edge.hop; 
        },
        get(edge_bundle, g));

    dijkstra_shortest_paths(g, destVertex,
        weight_map(wmap)
            .predecessor_map(make_iterator_property_map(p.begin(), get(vertex_index, g)))
            .distance_map(make_iterator_property_map(d.begin(), get(vertex_index, g))));

    for (phase_t i=0; i < network.topology.num_phases; ++i) {
        for (node_t from_node=0; from_node < network.topology.num_nodes; ++from_node) {
            switch_t any_switch = 0;
            port_t port = tgGraph->topology.port_of(from_node, any_switch);
            phase_t phase = tgGraph->phaseAdd(i, 1);

            auto currentVertex = tgGraph->vPN[tgGraph->pnIndex(i, from_node)];
            auto next = p[currentVertex];
            // Keep skipping phasenode until the hop.
            assert(!std::holds_alternative<tg::TPhaseNode>(g[next]));
            for (; std::holds_alternative<tg::TPhaseNode>(g[next]); next = p[next]) {
                assert(false); // Since we changed the temporal graph this should not be the case.
            }
            if (const auto *pNext = std::get_if<tg::TPort>(&g[next])) {
                port = pNext->port;
                phase = pNext->phase;
            }
            // Store for all flows that have that egress node.
            (*pChoiceCache)[{i, from_node, destination}] = {port, phase};
        }
    }
}

static ScheduleChoice cachedChoice(int32_t phase_i, int32_t from_node, node_t to_node) {
    auto key = ChoiceArgs{phase_i, from_node, to_node};
    auto iter = pChoiceCache->find(key);
    if (iter == pChoiceCache->end()) {
        computeToDestination(to_node);
        iter = pChoiceCache->find(key);
    }
    return iter->second;
}

packet_t scheduler_choice(node_t node, flow_t flow, phase_t phase, switch_t sw) {
    if (network.flows[flow].ingress == node) {
        // Random via point among immediately available nodes (send to a random switch).
        const auto random_switch = static_cast<switch_t>(hash_bounded(((phase << 16) + flow) ^ random_num, network.topology.num_switches));
        if (random_switch == sw) return 1;
    } else {
        // Quickest to egress
        auto choice = cachedChoice(phase, node, network.flows[flow].egress);
        auto port = network.topology.port_of(node, sw);
        if (phase == choice.phase && port == choice.port) return 1;
    }
    return 0;
}


void prepare_scheduler_choices() {
    random_num = random_num_simulation ^ network.buffers.get_buffer_hash();  // UPPAAL requires deterministic functions, so we use buffers (input to the API function) to generate a hash to use as the random number.
}

void init_scheduler() {
    if (!tgGraph) {
        tgGraph = std::make_unique<tg::TemporalGraph>(network.topology);
        pChoiceCache = std::make_unique<std::unordered_map<ChoiceArgs, ScheduleChoice>>();
    }
    random_num_simulation = random_gen();
}