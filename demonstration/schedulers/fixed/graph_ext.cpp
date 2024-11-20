#include "ext.hpp"
#include "temporal_graph.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/reverse_graph.hpp>


enum APPROACH { quickest,
    fewest_hops };
struct Params {
    APPROACH approach = quickest;
};

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


std::unique_ptr<tg::TemporalGraph> tgGraph;
Params params{quickest};
std::unique_ptr<std::unordered_map<ChoiceArgs, ScheduleChoice>> pChoiceCache;

class EnvVarException : public std::exception {
public:
    virtual const char *what() const noexcept { return "Bad ENV var set"; }
};

void readEnvVars() {
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

template <class P>
std::vector<tg::TVertex> outNeighbours(tg::Graph::vertex_descriptor from,
    P pred) {
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

port_t findOwnedPort(const Topology &topology, node_t owner) {
    for (size_t i = 0; i < topology.portOwner.size(); ++i) {
        if (topology.portOwner[i] == owner) {
            return i;
        }
    }
    return 0;
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
}

static ScheduleChoice cachedChoice(int32_t phase_i, int32_t from_node, int32_t flow) {
    node_t egress = network.flows[flow].egress;
    auto key = ChoiceArgs{phase_i, from_node, egress};
    auto iter = pChoiceCache->find(key);
    if (iter == pChoiceCache->end()) {
        computeToDestination(egress);
        iter = pChoiceCache->find(key);
    }
    return iter->second;
}

void customGetScheduleChoice(int32_t phase_i, int32_t node, int32_t flow,
    int32_t &choice_phase, int32_t &choice_port) {
    auto choice = cachedChoice(phase_i, node, flow);
    choice_phase = choice.phase;
    choice_port = choice.port;
}

void customPrepareChoices() {}

void customSetup() {
    readEnvVars();
    tgGraph = std::make_unique<tg::TemporalGraph>(network.topology);
    pChoiceCache =
        std::make_unique<std::unordered_map<ChoiceArgs, ScheduleChoice>>();
}

void customBegin() {}
