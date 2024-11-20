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

/*
    For each flow generate K solutions by
        - generating inverse djikstra for each goal
        - manually sort by weight and take the top K solutions.
    To make a choice.
        Each step for the simulation customPrepareChoices is made. We initialize a variable
        random_num from random_gen. This random_num is then using randomized hashing to select
        a given choice from the solutions.
*/

class EnvVarException : public std::exception {
public:
    virtual const char *what() const noexcept {
        return "Bad ENV var set";
    }
};

enum APPROACH { quickest,
    fewest_hops };
struct Params {
    int numPaths = 2;
    APPROACH approach = quickest;
};

class EgressSolution;
std::mt19937 random_gen;
// Random number chosen for this simulation step.
uint32_t random_num;
// Cached solutions
std::unique_ptr<std::vector<EgressSolution>> pSolutions;

Params params;

void readEnvVars() {
    if (const auto *envVal = std::getenv("CHOICE_NUM_PATHS")) {
        auto value = std::stoi(envVal);
        if (value >= 1 && value <= 8) {
            params.numPaths = value;
        } else {
            throw EnvVarException{};
        }
    }
    if (const auto *envVal = std::getenv("CHOICE_APPROACH")) {
        if (std::strcmp(envVal, "QUICKEST") == 0) {
            params.approach = quickest;
        } else if (std::strcmp(envVal, "FEWEST_HOPS") == 0) {
            params.approach = fewest_hops;
        } else {
            throw EnvVarException{};
        }
    }
}

// From: https://arxiv.org/abs/1504.06804
// hashes x strongly universally into the range [m]
uint32_t hash_bounded(uint32_t x, uint32_t m) {
    constexpr uint64_t a = 0x28ec0f222c79fb46; // uniformly selected
    constexpr uint64_t b = 0x2179c594b7d54ca2; // uniformly selected
    return (((a * x + b) >> 32) * m) >> 32;
}

struct PortInPhase {
    port_t port;
    phase_t phase;
};

struct NodeInPhase {
    node_t node;
    phase_t phase;
    bool operator==(const NodeInPhase &other) const {
        return node == other.node && phase == other.phase;
    }
};

namespace std {
template <>
struct hash<PortInPhase> {
    std::size_t operator()(const PortInPhase &s) const noexcept {
        return s.port << 16 | s.phase;
    }
};
template <>
struct hash<NodeInPhase> {
    std::size_t operator()(const NodeInPhase &s) const noexcept {
        return s.node << 16 | s.phase;
    }
};
}; // namespace std

struct EgressSolution {
    ScheduleChoice getChoice(phase_t phase, node_t node) {
        auto cpy = sol_.find({node, phase})->second;
        // Make random choice
        assert(cpy.size() > 0);
        auto index = hash_bounded(((phase << 16) + node) ^ random_num, cpy.size());
        const auto &choice = cpy[index];
        ScheduleChoice result;
        result.phase = choice.phase;
        result.port = choice.port;
        return result;
    }

    void storeBest(NodeInPhase nip, const std::vector<PortInPhase> &best) {
        sol_[nip] = best;
    }

private:
    std::unordered_map<NodeInPhase, std::vector<PortInPhase>> sol_;
};

EgressSolution constructSolutionForEgress(tg::TemporalGraph &tgGraph, node_t egress_node, const Params &params) {
    using namespace boost;

    EgressSolution solution;

    auto destVertex = tgGraph.vNodes[egress_node];
    // We reverse the graph to find all solutions to this node.
    auto revg = make_reverse_graph(tgGraph.graph);
    std::vector<tg::Graph::vertex_descriptor> p(num_vertices(revg));
    std::vector<int> d(num_vertices(revg));

    const auto approach = params.approach;
    auto edgeCost = [approach](const tg::TEdge &edge) {
        if (approach == fewest_hops) {
            return 10'000 * edge.hop + edge.time;
        }
        return 10'000 * edge.time + edge.hop;
    };

    auto wmap = make_transform_value_property_map(edgeCost, get(edge_bundle, revg));

    dijkstra_shortest_paths(revg, destVertex,
        weight_map(wmap)
            .predecessor_map(make_iterator_property_map(p.begin(), get(vertex_index, revg)))
            .distance_map(make_iterator_property_map(d.begin(), get(vertex_index, revg))));

    std::vector<std::pair<tg::TPort, int>> neighbours;
    std::vector<PortInPhase> options;
    std::vector<port_t> seenPorts;
    options.reserve(params.numPaths);
    auto &g = tgGraph.graph;
    // Take lowest weighted paths.
    for (phase_t phase = 0; phase < network.parameters.num_phases; ++phase) {
        for (node_t node = 0; node < network.parameters.num_nodes; ++node) {
            auto key = NodeInPhase{node, phase};
            options.clear();
            neighbours.clear();
            seenPorts.clear();
            auto curVertex = tgGraph.vPN[tgGraph.pnIndex(phase, node)];

            for (auto [e, e_end] = out_edges(curVertex, g); e != e_end; ++e) {
                auto vNext = target(*e, g);
                auto next = g[vNext];
                if (auto *pn = std::get_if<tg::TPort>(&next)) {
                    // Remember to add the edge weight too!
                    auto cost = edgeCost(g[*e]) + d[vNext];
                    neighbours.push_back({*pn, cost});
                }
            }
            std::stable_sort(neighbours.begin(), neighbours.end(), [&](const auto &a, const auto &b) {
                return a.second < b.second;
            });

            // Take the k best
            for (size_t i = 0; i < neighbours.size(); ++i) {
                const auto &next = neighbours[i].first;
                if (std::find(seenPorts.begin(), seenPorts.end(), next.port) != seenPorts.end()) {
                    continue;
                }
                options.push_back({next.port,
                    next.phase});
                seenPorts.push_back(next.port);
                if (options.size() >= (size_t)params.numPaths) {
                    break;
                }
            }
            solution.storeBest(key, options);
        }
    }

    return solution;
}

void constructSolutions() {
    pSolutions = std::make_unique<std::vector<EgressSolution>>();
    pSolutions->reserve(network.parameters.num_flows);
    auto tg = tg::TemporalGraph(network.topology);

    // Go through all node as a potential egress node, and update flow solutions for all matching flows.
    pSolutions->resize(network.parameters.num_flows);
    for (node_t egress_node = 0; egress_node < network.parameters.num_nodes; ++egress_node) {
        bool is_destination = std::any_of(network.flows.begin(), network.flows.end(), [egress_node](const auto &f) {
            return f.egress == egress_node;
        });
        if (is_destination) {
            auto solution = constructSolutionForEgress(tg, egress_node, params);
            // Update solutions
            for (int32_t flow = 0; flow < network.parameters.num_flows; ++flow) {
                if (network.flows[flow].egress == egress_node) {
                    (*pSolutions)[flow] = solution;
                }
            }
        }
    }
}

void customGetScheduleChoice(int32_t phase_i, int32_t node, int32_t flow, int32_t &choice_phase, int32_t &choice_port) {
    auto &flow_solution = (*pSolutions)[flow];
    auto choice = flow_solution.getChoice(phase_i, node);
    choice_phase = choice.phase;
    choice_port = choice.port;
}

void customPrepareChoices() {
    random_num = random_gen();
}

void customSetup() {
    readEnvVars();
    constructSolutions();
}

void customBegin() {
    random_gen = std::mt19937(123456);
}
