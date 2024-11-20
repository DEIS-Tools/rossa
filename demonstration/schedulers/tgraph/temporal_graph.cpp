#include <ostream>

#include "ext.hpp"
#include "temporal_graph.hpp"

const int NUM_PHASES = 4;
const int NUM_NODES = 5;
const int NUM_FLOWS = 5;
const int NUM_PORTS = 10;

const node_t TOPOLOGY[NUM_PHASES][NUM_PORTS] = {
    {1, 3, 2, 4, 3, 0, 4, 1, 0, 2},
    {2, 4, 3, 0, 4, 1, 0, 2, 1, 3},
    {3, 1, 4, 2, 0, 3, 1, 4, 2, 0},
    {4, 2, 0, 3, 1, 4, 2, 0, 3, 1}};

namespace tg
{
    Topology fromTestData()
    {
        Topology tp;
        tp.num_phases = NUM_PHASES;
        tp.num_ports = NUM_PORTS;
        tp.num_nodes = NUM_NODES;
        tp.resizeLimits();
        for (int i = 0; i < tp.num_phases; ++i)
        {
            tp.pushTopology(i, TOPOLOGY[i]);
        }
        for (int owner= 0; owner < tp.num_nodes; ++owner) {
            tp.portOwner[owner*2] = owner;
            tp.portOwner[owner*2+1] = owner;
        }
        return tp;
    }


    TemporalGraph::TemporalGraph(Topology topology_) : topology(topology_)
    {
        createVertices();
        createWaits();
        createTransfers();
        createCollectorNodeEdges();
    }

    void TemporalGraph::createVertices()
    {
        // Create bare nodes
        for (int32_t node = 0; node < topology.num_nodes; ++node)
        {
            auto vDescriptor = add_vertex(graph);
            graph[vDescriptor] = TNode{node};
            vNodes.push_back(vDescriptor);
        }
        // Create phase nodes
        for (int32_t phase = 0; phase < topology.num_phases; ++phase)
        {
            for (int32_t node = 0; node < topology.num_nodes; ++node)
            {
                auto vDescriptor = add_vertex(graph);
                graph[vDescriptor] = TPhaseNode{phase, node};
                vPN.push_back(vDescriptor);
            }
        }
        // Create phase ports
        for (int32_t phase = 0; phase < topology.num_phases; ++phase)
        {
            for (int32_t port = 0; port < topology.num_ports; ++port)
            {
                auto vDescriptor = add_vertex(graph);
                graph[vDescriptor] = TPort{phase, port};
                vPP.push_back(vDescriptor);
            }
        }
    }

    void TemporalGraph::createWaits()
    {
        // Disabled: Instead we create transitions to already waited-for vertices.
    }

    /*
        Connects all phases nodes with their master collector node.
        The main node eases shortest path searching.
    */
    void TemporalGraph::createCollectorNodeEdges()
    {
        for (int32_t phase = 0; phase < topology.num_phases; ++phase)
        {
            for (int32_t node = 0; node < topology.num_nodes; ++node)
            {
                auto vFrom = vPN[pnIndex(phase, node)];
                auto vTo = vNodes[nIndex(node)];
                auto eDescriptor = add_edge(vFrom, vTo, graph).first;
                graph[eDescriptor] = TEdge{0, 0, 0};
            }
        }
    }

    void TemporalGraph::createTransfers()
    {
        // phase ports make hops to their destination node
        for (int32_t phase = 0; phase < topology.num_phases; ++phase)
        {
            for (int32_t port = 0; port < topology.num_ports; ++port)
            {
                node_t target = topology(phase, port);
                auto vFrom = vPP[ppIndex(phase, port)];
                // Arrives next phase
                phase_t arrivePhase = phaseAdd(phase, 1);
                auto vTo = vPN[pnIndex(arrivePhase, target)];
                auto eDescriptor = add_edge(vFrom, vTo, graph);
                // Cost a jump and phase shift.
                graph[eDescriptor.first] = TEdge{1, 1, 0};
                assert(eDescriptor.second);
            }
        }
        // phase nodes puts packets in phase port
        for (int32_t phase = 0; phase < topology.num_phases; ++phase)
        {
            for (int32_t port = 0; port < topology.num_ports; ++port)
            {
                node_t owner = topology.owner(port);
                auto vFrom = vPN[pnIndex(phase, owner)];
                for (int32_t waitTime=1; waitTime <= topology.num_phases; ++waitTime) {
                    auto targetPhase = phaseAdd(phase, waitTime);
                    auto vTo = vPP[ppIndex(targetPhase, port)];
                    auto eDescriptor = add_edge(vFrom, vTo, graph);
                    graph[eDescriptor.first] = TEdge{waitTime, 0, 1};
                    assert(eDescriptor.second);
                }
            }
        }
    }
}
