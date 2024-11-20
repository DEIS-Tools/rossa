#pragma once

#include "ext.hpp"

#include <variant>
#include <vector>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>

namespace tg
{
    struct TPort {
        phase_t phase;
        port_t port;
    };

    struct TNode {
        node_t node;
    };

    struct TPhaseNode {
        phase_t phase;
        node_t node;
    };

    using TVertex = std::variant<TNode, TPhaseNode, TPort>;

    struct TEdge {
        int time;
        int hop;
        int delay = 0; // Not used to measure time itself.
    };

    using namespace boost;
    using Graph = adjacency_list<vecS, vecS, bidirectionalS, TVertex, TEdge>;

    struct TemporalGraph
    {
        TemporalGraph(Topology topology_);

        void makeWaitEdges();
        void makeSendEdges();

        phase_t phaseAdd(phase_t p, phase_t add) {
            return (p + add) % topology.num_phases;
        }

        Topology topology;
        Graph graph;
        std::vector<Graph::vertex_descriptor> vNodes;
        std::vector<Graph::vertex_descriptor> vPN;
        std::vector<Graph::vertex_descriptor> vPP;
        size_t pnIndex(phase_t phase, node_t node) {
            return phase * topology.num_nodes + node;
        }
        size_t ppIndex(phase_t phase, port_t port) {
            return phase * topology.num_ports + port;
        }
        size_t nIndex(node_t node) {
            return node;
        }
        private:
        void createVertices();
        void createWaits();
        void createTransfers();
        void createCollectorNodeEdges();
    };

    Topology fromTestData();
}


#include <boost/graph/graphviz.hpp>

struct DotAbbreviation
{

    DotAbbreviation(std::ostream &ostream) : out(ostream) {}

    void operator()(const tg::TNode &d)
    {
        out << "N(" << d.node << ")";
    }
    void operator()(const tg::TPhaseNode &d)
    {
        out << "PN(" << d.phase << "," << d.node << ")";
    }
    void operator()(const tg::TPort &d)
    {
        out << "P(" << d.phase << "," << d.port << ")";
    }

private:
    std::ostream &out;
};

class label_writer
{
public:
    label_writer(tg::Graph &_graph) : graph(_graph) {}
    void operator()(std::ostream &out, const tg::Graph::vertex_descriptor &v) const
    {
        out << "[label=\"";
        std::visit(DotAbbreviation(out), graph[v]);
        out << "\"]";
    }

private:
    tg::Graph &graph;
};

