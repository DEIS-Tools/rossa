#include "ext.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/reverse_graph.hpp>

#include "temporal_graph.hpp"

/*
    For each flow generate UP-TO K solutions by
        - generating inverse djikstra for each goal
        - manually sort by weight and take the top K solutions.
        - also we force each path to use another port so K > num_switches have no meaning.
*/

class EnvVarException : public std::exception {
public:
    virtual const char *what() const noexcept {
        return "Bad ENV var set";
    }
};

class FlowSolution;
enum APPROACH { quickest,
    fewest_hops };

std::unique_ptr<std::vector<FlowSolution>> pSolutions;

struct Params {
    APPROACH approach;
    int numPaths;
    double alternativeTreshold;
};

Params params{
    quickest,
    2,
    0.7};

void readEnvVars() {
    if (const auto *envVal = std::getenv("CAPACITY_NUM_PATHS")) {
        auto value = std::stoi(envVal);
        if (value >= 1 && value <= 8) {
            params.numPaths = value;
        } else {
            throw EnvVarException{};
        }
    }
    if (const auto *envVal = std::getenv("CAPACITY_APPROACH")) {
        if (std::strcmp(envVal, "QUICKEST") == 0) {
            params.approach = quickest;
        } else if (std::strcmp(envVal, "FEWEST_HOPS") == 0) {
            params.approach = fewest_hops;
        } else {
            throw EnvVarException{};
        }
    }
    if (const auto *envVal = std::getenv("CAPACITY_TRESHOLD")) {
        auto value = std::stod(envVal);
        if (value > 0 && value <= 100.0) {
            params.alternativeTreshold = value;
        } else {
            throw EnvVarException{};
        }
    }
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

struct FlowSolution {
    ScheduleChoice getChoice(phase_t phase, node_t node, const PortLoad &loads) const {
        auto cpy = sol_.find({node, phase})->second;
        const auto treshold = params.alternativeTreshold;
        auto choice = std::find_if(cpy.begin(), cpy.end(), [treshold, &loads](const auto &pip) {
            return loads.getTotalPortLoad(pip.port) < treshold;
        });
        // If no-one matches take first solution.
        const auto &best = choice == cpy.end() ? cpy[0] : *choice;
        ScheduleChoice result;
        result.phase = best.phase;
        result.port = best.port;
        return result;
    }

    void storeBest(NodeInPhase nip, const std::vector<PortInPhase> &best) {
        sol_[nip] = best;
    }

private:
    std::unordered_map<NodeInPhase, std::vector<PortInPhase>> sol_;
};

FlowSolution constructSolutionForFlow(tg::TemporalGraph &tgGraph, flow_t flow, const Params &params) {
    using namespace boost;

    FlowSolution solution;

    auto netDestination = network.flows[flow].egress;
    auto destVertex = tgGraph.vNodes[netDestination];

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

    // With the reversed graph we have the cost distances to the egress marker node.

    // Reused storage
    std::vector<std::pair<tg::TPort, int>> neighbours;
    std::vector<PortInPhase> options;
    std::vector<port_t> portsUsed;
    options.reserve(params.numPaths);
    portsUsed.reserve(params.numPaths);

    // For each node in each phase we can now compute the choice.
    auto &g = tgGraph.graph;
    for (phase_t phase = 0; phase < network.parameters.num_phases; ++phase) {
        for (node_t node = 0; node < network.parameters.num_nodes; ++node) {
            options.clear();
            neighbours.clear();
            portsUsed.clear();

            // We take all out targets and sort them by distance.
            const auto vCurrent = tgGraph.vPN[tgGraph.pnIndex(phase, node)];
            for (auto [e, e_end] = out_edges(vCurrent, g); e != e_end; ++e) {
                auto vNext = target(*e, g);
                auto tgNext = g[vNext];
                if (auto *pn = std::get_if<tg::TPort>(&tgNext)) {
                    // Remember to add the edge weight too!
                    auto cost = edgeCost(g[*e]) + d[vNext];
                    neighbours.push_back({*pn, cost});
                }
            }
            std::stable_sort(neighbours.begin(), neighbours.end(), [&](const auto &a, const auto &b) {
                return a.second < b.second;
            });

            // Take the k best but don't reuse same port!
            for (size_t i = 0; i < neighbours.size(); ++i) {
                const auto &next = neighbours[i].first;
                if (std::find(portsUsed.begin(), portsUsed.end(), next.port) != portsUsed.end()) {
                    continue;
                }
                options.push_back({next.port,
                    next.phase});
                portsUsed.push_back(next.port);
                if (options.size() >= (size_t)params.numPaths) {
                    break;
                }
            }
            auto solutionKey = NodeInPhase{node, phase};
            solution.storeBest(solutionKey, options);
        }
    }

    return solution;
}

void constructSolutions() {
    pSolutions = std::make_unique<std::vector<FlowSolution>>();
    pSolutions->reserve(network.parameters.num_flows);

    auto tg = tg::TemporalGraph(network.topology);
    for (flow_t flow = 0; flow < network.parameters.num_flows; ++flow) {
        pSolutions->push_back(constructSolutionForFlow(tg, flow, params));
    }
}

void customGetScheduleChoice(int32_t phase_i, int32_t node, int32_t flow, int32_t &choice_phase, int32_t &choice_port) {
    const auto &flow_solution = (*pSolutions)[flow];
    PortLoad loads;
    auto choice = flow_solution.getChoice(phase_i, node, loads);
    choice_phase = choice.phase;
    choice_port = choice.port;
}

void customPrepareChoices() {
}

void customSetup() {
    readEnvVars();
    constructSolutions();
}

void customBegin() {
}
