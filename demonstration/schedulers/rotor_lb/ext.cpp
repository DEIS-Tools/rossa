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


std::mt19937 random_gen;
// Random number chosen for this simulation step.
uint32_t random_num;

// std::unique_ptr<tg::TemporalGraph> tgGraph;
std::unique_ptr<std::unordered_map<ChoiceArgs, SchedulerChoice>> pChoiceCache = nullptr;


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

// class RlbMaster {
// public:
//     explicit RlbMaster(int phase_i) : _phase_i(phase_i) {
//         _current_commit_queue = 0; // the topology is defined to start here (Rotor switch 0)
//
//         _H = network.parameters.num_ports; // number of hosts
//         _N = network.parameters.num_nodes; // number of racks
//         _hpr = 1; //network.parameters.num_switches(); // number of hosts per rack
//
//         _working_queue_sizes.resize(_H);
//         for (int i = 0; i < _H; i++) {
//             _working_queue_sizes[i].resize(2);
//             for (int j = 0; j < 2; j++) {
//                 _working_queue_sizes[i][j].resize(_H);
//             }
//         }
//
//         _q_inds.resize(_H);
//         for (int i = 0; i < _H; i++)
//             _q_inds[i].resize(2);
//
//         _link_caps_send.resize(_H);
//         _link_caps_recv.resize(_H);
//
//         _Nsenders.resize(_H);
//
//         _pkts_to_send.resize(_H);
//
//         _dst_labels.resize(_H);
//
//         _proposals.resize(_H);
//
//         _accepts.resize(_H);
//         for (int i = 0; i < _H; i++)
//             _accepts[i].resize(_H);
//     }
//
//     // void start();
//     // void doNextEvent();
//     void newMatching();
//
//     void phase1(std::vector<int> src_hosts, std::vector<int> dst_hosts);
//     void phase2(std::vector<int> src_hosts, std::vector<int> dst_hosts);
//
//     std::vector<int> fairshare1d(std::vector<int> input, int cap1, bool extra);
//     std::vector<std::vector<int>> fairshare2d(std::vector<std::vector<int>> input, std::vector<int> cap0, std::vector<int> cap1);
//     std::vector<std::vector<int>> fairshare2d_2(std::vector<std::vector<int>> input, int cap0, std::vector<int> cap1);
//
//     int _phase_i;
//
//     // DynExpTopology* _top;
//     int _current_commit_queue;
//
//     int _H;
//     int _N;
//     int _hpr;
//     int _max_pkts;
//
//     std::vector<std::vector<std::vector<int>>> _working_queue_sizes;
//
//     std::vector<int> _link_caps_send; // link capacity (in packets) for each sending node
//     std::vector<int> _link_caps_recv; // link capacity (in packets) for each receiving node
//     std::vector<int> _Nsenders; // number of queues sending for each node
//
//     std::vector<std::vector<int>> _pkts_to_send; // dims: (host) x ??? number of senders
//     std::vector<std::vector<std::vector<int>>> _q_inds; // dims: (host) x (rlb queue index[i][j])
//
//     std::vector<std::vector<int>> _proposals; // dims: (host) x ...
//     std::vector<std::vector<std::vector<int>>> _accepts; // dims: (sending host) x (receiving host) x ...
//
//     std::vector<std::vector<int>> _dst_labels; // dims: (host) x ...
//
//
//     // std::vector<> _commits;
//
// };
// void RlbMaster::newMatching() {
// /*
//     // get the current slice:
//     // first, get the current "superslice"
//     int64_t superslice = (eventlist().now() / _top->get_slicetime(3)) %
//     _top->get_nsuperslice();
//     // next, get the relative time from the beginning of that superslice
//     int64_t reltime = eventlist().now() - superslice*_top->get_slicetime(3) -
//         (eventlist().now() / (_top->get_nsuperslice()*_top->get_slicetime(3))) *
//         (_top->get_nsuperslice()*_top->get_slicetime(3));
//     int slice; // the current slice
//     if (reltime < _top->get_slicetime(0))
//         slice = 0 + superslice*3;
//     else if (reltime < _top->get_slicetime(0) + _top->get_slicetime(1))
//         slice = 1 + superslice*3;
//     else
//         slice = 2 + superslice*3;
// */
//
//     // debug:
//     //cout << "-*-*- New matching at slice = " << slice << ", time = " << timeAsUs(eventlist().now()) << " us -*-*-" << endl;
//
//     // copy queue sizes from each host into a datastructure that covers all hosts
//     // RlbModule* mod;
//     for (int host = 0; host < _H; host++) {
//         // mod = _top->get_rlb_module(host); // get pointer to module
//         // std::vector<std::vector<int>> _queue_sizes_temp = mod->get_queue_sizes(); // get queue sizes from module
//         for (int j = 0; j < 2; j++) {
//             for (int k = 0; k < _H; k++)
//                 _working_queue_sizes[host][j][k] = 0;
//         }
//         for (flow_t flow = 0; flow < network.parameters.num_flows; flow++) {
//             if (network.flows[flow].ingress == host) {
//                 _working_queue_sizes[host][1][network.flows[flow].egress] += PortLoad::getPacketsForFlow(host, flow);
//             } else {
//                 _working_queue_sizes[host][0][network.flows[flow].egress] += PortLoad::getPacketsForFlow(host, flow);
//             }
//
//         }
//     }
//
//     // copy some other parameters over:
//     // mod = _top->get_rlb_module(0);
//     // _max_pkts = mod->get_max_pkts();
//
//     // clear any previous queue indices
//     for (int i = 0; i < _H; i++)
//         for (int j = 0; j < 2; j++)
//             _q_inds[i][j].resize(0);
//
//     // clear any previous packets-to-send counters
//     for (int i = 0; i < _H; i++)
//         _pkts_to_send[i].resize(0);
//
//     // clear any previous _dst_labels
//     for (int i = 0; i < _H; i++)
//         _dst_labels[i].resize(0);
//
//     // clear any previous _proposals
//     for (int i = 0; i < _H; i++)
//         _proposals[i].resize(0);
//
//     // clear any previous _accpets
//     for (int i = 0; i < _H; i++)
//         for (int j = 0; j < _H; j++)
//             _accepts[i][j].resize(0);
//
//     // initialize link capacities to full capacity:
//     for (int i = 0; i < _H; i++) {
//         _link_caps_send[i] = _max_pkts;
//         _link_caps_recv[i] = _max_pkts;
//     }
//
//
//
//     // ---------- phase 1 ---------- //
//
//     for (int crtToR = 0; crtToR < _N; crtToR++) {
//
//         // get the list of new dst hosts (that every host in this rack is now connected to)
//         int dstToR = network.topology(_phase_i, network.parameters.port_of(crtToR, sw)); // _top->get_nextToR(slice, crtToR, _current_commit_queue + _hpr);
//
//         if (crtToR != dstToR) { // necessary because of the way we define the topology
//
//             std::vector<int> dst_hosts;
//             int basehost = dstToR * _hpr;
//             for (int j = 0; j < _hpr; j++) {
//                 dst_hosts.push_back(basehost + j);
//             }
//
//             // get the list of src hosts in this rack
//             std::vector<int> src_hosts;
//             basehost = crtToR * _hpr;
//             for (int j = 0; j < _hpr; j++) {
//                 src_hosts.push_back(basehost + j);
//             }
//
//             // get:
//             // 1. the 2nd hop "rates"
//             // 2. the 1 hop "rates"
//             // 3. the 1st hop proposed "rates"
//             // all "rates" are in units of packets
//
//             phase1(src_hosts, dst_hosts);
//         }
//     }
//
//
//     // ---------- phase 2 ---------- //
//
//     for (int crtToR = 0; crtToR < _N; crtToR++) {
//
//         // get the list of new dst hosts (that every host in this rack is now connected to)
//         int dstToR = network.topology(_phase_i, network.parameters.port_of(crtToR, sw));  // _top->get_nextToR(slice, crtToR, _current_commit_queue + _hpr);
//
//         if (crtToR != dstToR) {
//
//             std::vector<int> dst_hosts;
//             int basehost = dstToR * _hpr;
//             for (int j = 0; j < _hpr; j++) {
//                 dst_hosts.push_back(basehost + j);
//             }
//
//             // get the list of src hosts in this rack
//             std::vector<int> src_hosts;
//             basehost = crtToR * _hpr;
//             for (int j = 0; j < _hpr; j++) {
//                 src_hosts.push_back(basehost + j);
//             }
//
//             // given the proposals, generate the accepts
//
//             phase2(src_hosts, dst_hosts); // the dst_hosts compute how much to accept from the src_hosts
//         }
//     }
//
//
//     // ---------- phase 3 ---------- //
//
//     for (int crtToR = 0; crtToR < _N; crtToR++) {
//
//         // get the list of new dst hosts (that every host in this rack is now connected to)
//         int dstToR = network.topology(_phase_i, network.parameters.port_of(crtToR, _current_commit_queue));  // _top->get_nextToR(slice, crtToR, _current_commit_queue + _hpr);
//
//         if (crtToR != dstToR) {
//
//             std::vector<int> dst_hosts;
//             int basehost = dstToR * _hpr;
//             for (int j = 0; j < _hpr; j++) {
//                 dst_hosts.push_back(basehost + j);
//             }
//
//             // get the list of src hosts in this rack
//             std::vector<int> src_hosts;
//             basehost = crtToR * _hpr;
//             for (int j = 0; j < _hpr; j++) {
//                 src_hosts.push_back(basehost + j);
//             }
//
//             // given the accepts, finish getting how many packets to send and such
//             // and communicate this to the RlbModule::enqueue_commit()
//
//             for (int j = 0; j < src_hosts.size(); j++){
//                 for (int k = 0; k < dst_hosts.size(); k++) {
//
//                     for (int l = 0; l < _H; l++) {
//                         int temp = _accepts[src_hosts[j]][dst_hosts[k]][l];
//                         if (temp > 0) {
//                             _Nsenders[src_hosts[j]] ++; // increment sender count
//                             _pkts_to_send[src_hosts[j]].push_back(temp); // how many packets we should send
//                             _q_inds[src_hosts[j]][0].push_back(1); // first index (0 = nonlocal, 1 = local)
//                             _q_inds[src_hosts[j]][1].push_back(l); // second index (which queue)
//                             _dst_labels[src_hosts[j]].push_back(dst_hosts[k]);
//
//                             // debug:
//                             //cout << "RlbMaster - src: " << src_hosts[j] << ", dst: " << dst_hosts[k] << endl;
//                             //cout << "   send " << _pkts_to_send[src_hosts[j]].back() << " packets from local queue " << l << endl;
//
//                             //if (src_hosts[j] == 0 && timeAsUs(eventlist().now()) == 5044) {
//                             //    cout << "   *Master: src_host: " << src_hosts[j] << " at " << timeAsUs(eventlist().now()) << " us." << endl;
//                             //    cout << "    > send to dst_host = " << dst_hosts[k] << endl;
//                             //    cout << "      > from queue = [" << _q_inds[src_hosts[j]][0].back() << "][" << _q_inds[src_hosts[j]][1].back() << "]" << endl;
//                             //    cout << "      > # packets = " << _pkts_to_send[src_hosts[j]].back() << endl;
//                             //}
//
//
//                         }
//                     }
//                 }
//
//                 // only start if there's something to send...
//                 if (_Nsenders[src_hosts[j]] > 0) {
//                     // mod = _top->get_rlb_module(src_hosts[j]);
//                     // _commits.emplace_back(_current_commit_queue, _Nsenders[src_hosts[j]], _pkts_to_send[src_hosts[j]], _q_inds[src_hosts[j]], _dst_labels[src_hosts[j]]);
//                 }
//             }
//         }
//
//     }
//
//
//     // --------- set up next new matching --------- //
//
//     _current_commit_queue++;
//     _current_commit_queue = _current_commit_queue % _hpr;
// /*
//     // set up the next reconfiguration event:
//     eventlist().sourceIsPendingRel(*this, _top->get_slicetime(3));
//     */
// }
//
//
// void RlbMaster::phase1(std::vector<int> src_hosts, std::vector<int> dst_hosts)
// {
//
//     // -------------------------------------------------------------------- //
//
//     // STEP 1 - decide how many 2nd hop packets to send.
//     // this is done on a host level:
//
//     int fixed_pkts = _max_pkts / _hpr; // maximum "safe" number of packets we can send
//
//     for (int i = 0; i < src_hosts.size(); i++) { // sweep senders
//
//         _Nsenders[src_hosts[i]] = 0; // initialize number of sending queues to zero
//
//         for (int j = 0; j < dst_hosts.size(); j++) { // sweep destinations
//
//             int temp_pkts = _working_queue_sizes[src_hosts[i]][0][dst_hosts[j]]; // 0 = nonlocal
//             if (temp_pkts > fixed_pkts)
//                 temp_pkts = fixed_pkts; // limit to the maximum amount we can safely send
//
//             _link_caps_send[src_hosts[i]] -= temp_pkts; // uses some of sender's capacity
//             _link_caps_recv[dst_hosts[j]] -= temp_pkts; // uses some of receiver's capacity
//             _working_queue_sizes[src_hosts[i]][0][dst_hosts[j]] -= temp_pkts;
//
//             if (temp_pkts > 0) {
//                 _Nsenders[src_hosts[i]] ++; // increment sender count
//                 _pkts_to_send[src_hosts[i]].push_back(temp_pkts); // how many packets we should send
//
//                 _q_inds[src_hosts[i]][0].push_back(0); // first index (0 = nonlocal, 1 = local)
//                 _q_inds[src_hosts[i]][1].push_back(dst_hosts[j]); // second index (which queue)
//
//                 _dst_labels[src_hosts[i]].push_back(dst_hosts[j]);
//             }
//         }
//     }
//
//     // -------------------------------------------------------------------- //
//
//     // STEP 2 - decide how many 1 hop packets to send.
//     // this is done at a rack level:
//
//     // get the local queue matrix:
//     std::vector<std::vector<int>> local_pkts_rack;
//     local_pkts_rack.resize(src_hosts.size());
//     for (int i = 0; i < src_hosts.size(); i++)
//         local_pkts_rack[i].resize(dst_hosts.size());
//
//     for (int i = 0; i < src_hosts.size(); i++) {
//         for (int j = 0; j < dst_hosts.size(); j++) {
//             local_pkts_rack[i][j] = _working_queue_sizes[src_hosts[i]][1][dst_hosts[j]]; // 1 = local
//         }
//     }
//
//     // get vectors of the capacities:
//     std::vector<int> src_caps;
//     for (int i = 0; i < src_hosts.size(); i++)
//         src_caps.push_back(_link_caps_send[src_hosts[i]]);
//
//     // get vectors of the capacities:
//     std::vector<int> dst_caps;
//     for (int i = 0; i < dst_hosts.size(); i++)
//         dst_caps.push_back(_link_caps_recv[dst_hosts[i]]);
//
//
//     /*
//     // debug:
//     if (src_hosts[0] == 0 && dst_hosts[0] == 276) { // the first rack is sending
//         cout << "src_caps = ";
//         for (int i = 0; i < src_hosts.size(); i++)
//             cout << src_caps[i] << " ";
//         cout << endl;
//         cout << "dst_caps = ";
//         for (int i = 0; i < src_hosts.size(); i++)
//             cout << dst_caps[i] << " ";
//         cout << endl << "input[src_host, dst_host] =" << endl;
//         for (int i = 0; i < src_hosts.size(); i++) {
//             for (int j = 0; j < dst_hosts.size(); j++)
//                 cout << local_pkts_rack[i][j] << " ";
//             cout << endl;
//         }
//     }
//     */
//
//     // fairshare across both dimensions
//     local_pkts_rack = fairshare2d(local_pkts_rack, src_caps, dst_caps);
//
//     /*
//     // ------------------------------------------------
//     // debug:
//     if (src_hosts[0] == 0 && dst_hosts[0] == 276) { // the first rack is sending
//         cout << "dest_hosts: ";
//         for (int i = 0; i < dst_hosts.size(); i++)
//             cout << " " << dst_hosts[i];
//         cout << endl;
//         for (int i = 0; i < src_hosts.size(); i++) {
//             cout << "src_host " << src_hosts[i] << ":";
//             for (int j = 0; j < dst_hosts.size(); j++)
//                 cout << " " << local_pkts_rack[i][j];
//             cout << endl;
//         }
//     }
//     // ------------------------------------------------
//     */
//
//     // update sending capacities
//     for (int i = 0; i < src_hosts.size(); i++) {
//         int temp = 0;
//         for (int j = 0; j < dst_hosts.size(); j++)
//             temp += local_pkts_rack[i][j];
//         _link_caps_send[src_hosts[i]] -= temp;
//     }
//
//     // update receiving capacities
//     for (int i = 0; i < dst_hosts.size(); i++) {
//         int temp = 0;
//         for (int j = 0; j < src_hosts.size(); j++)
//             temp += local_pkts_rack[j][i];
//         _link_caps_recv[dst_hosts[i]] -= temp;
//     }
//
//     // update sender count, packets sent, queue indices, and decrement queue sizes
//     for (int i = 0; i < src_hosts.size(); i++) {
//         for (int j = 0; j < dst_hosts.size(); j++) {
//             int temp_pkts = local_pkts_rack[i][j];
//             if (temp_pkts > 0) {
//                 // some packets can be sent:
//                 _Nsenders[src_hosts[i]] ++; // increment sender count
//                 _pkts_to_send[src_hosts[i]].push_back(temp_pkts); // how many packets we should send
//                 _q_inds[src_hosts[i]][0].push_back(1); // first index (0 = nonlocal, 1 = local)
//                 _q_inds[src_hosts[i]][1].push_back(dst_hosts[j]); // second index (which queue)
//                 _working_queue_sizes[src_hosts[i]][1][dst_hosts[j]] -= temp_pkts;
//                 _dst_labels[src_hosts[i]].push_back(dst_hosts[j]);
//             }
//         }
//     }
//
//     // -------------------------------------------------------------------- //
//
//     // STEP 3 - propose traffic
//
//     for (int i = 0; i < src_hosts.size(); i++) {
//         std::vector<int> temp = _working_queue_sizes[src_hosts[i]][1];
//         for (int j = 0; j < dst_hosts.size(); j++) {
//             temp[dst_hosts[j]] = 0; // remove any elements corresponding to hosts in the dest. rack
//         }
//         temp = fairshare1d(temp, _link_caps_send[src_hosts[i]], true); // fairshare according to remaining link capacity
//         _proposals[src_hosts[i]] = temp;
//     }
//
// }
//
// void RlbMaster::phase2(std::vector<int> src_hosts, std::vector<int> dst_hosts)
// {
//     // the dst_hosts compute how much to accept from the src_hosts
//
//     // NOTE: MODIFICATION: can add a factor of two, which helps with throughput (a little)
//     // This extra factor of two might delay traffic by 1 cycle though...
//     int fixed_pkts = 2 * _max_pkts / _hpr; // maximum "safe" number of packets we can have per destination
//     //int fixed_pkts = _max_pkts / _hpr; // maximum "safe" number of packets we can have per destination
//
//     for (int i = 0; i < dst_hosts.size(); i++) { // sweep destinations
//
//         std::vector<int> temp = _working_queue_sizes[dst_hosts[i]][0];
//         for (int j = 0; j < _H; j++) {
//             temp[j] += _working_queue_sizes[dst_hosts[i]][1][j];
//             temp[j] = fixed_pkts - temp[j];
//             if (temp[j] < 0) {
//                 temp[j] = 0;
//             }
//         }
//
//         std::vector<std::vector<int>> all_proposals;
//         all_proposals.resize(src_hosts.size());
//         for (int j = 0; j < src_hosts.size(); j++)
//             all_proposals[j] = _proposals[src_hosts[j]];
//
//         // fairshare, according to queue availability + receiving link capacity
//         all_proposals = fairshare2d_2(all_proposals, _link_caps_recv[dst_hosts[i]], temp);
//
//         for (int j = 0; j < src_hosts.size(); j++)
//             _accepts[src_hosts[j]][dst_hosts[i]] = all_proposals[j];
//     }
// }
//
// std::vector<int> RlbMaster::fairshare1d(std::vector<int> input, int cap1, bool extra) {
//
//     std::vector<int> sent;
//     sent.resize(input.size());
//     for (int i = 0; i < input.size(); i++)
//         sent[i] = input[i];
//
//     int nelem = 0;
//     for (int i = 0; i < input.size(); i++)
//         if (input[i] > 0)
//             nelem++;
//
//     if (nelem != 0) {
//         bool cont = true;
//         while (cont) {
//             int f = cap1 / nelem; // compute the fair share
//             int min = 0;
//             for (int i = 0; i < input.size(); i++) {
//                 if (input[i] > 0) {
//                     input[i] -= f;
//                     if (input[i] < 0)
//                         min = input[i];
//                 }
//             }
//             cap1 -= f * nelem;
//             if (min < 0) { // some elements got overserved
//                 //cap1 = 0;
//                 for (int i = 0; i < input.size(); i++)
//                     if (input[i] < 0) {
//                         cap1 += (-1) * input[i];
//                         input[i] = 0;
//                     }
//                 nelem = 0;
//                 for (int i = 0; i < input.size(); i++)
//                     if (input[i] > 0)
//                         nelem++;
//                 if (nelem == 0) {
//                     cont = false;
//                 }
//             } else {
//                 cont = false;
//             }
//         }
//     }
//
//     nelem = 0;
//     for (int i = 0; i < input.size(); i++)
//         if (input[i] > 0)
//             nelem++;
//
//     if (nelem != 0 && cap1 > 0 && extra) {
//
//         // debug:
//         /*cout << "nelem = " << nelem << ", cap1 = " << cap1 << ", input.size() = " << input.size() << endl;
//         cout << "input @ extra =";
//         for (int i = 0; i < input.size(); i++)
//             cout << " " << input[i];
//         cout << endl;*/
//
//         // randomly assign any remainders
//         while (cap1 > 0) {
//             int nelem = 0;
//             std::vector <int> inds; // queue indices that still want packets
//             for (int i = 0; i < input.size(); i++)
//                 if (input[i] > 0) {
//                     inds.push_back(i);
//                     nelem++;
//                 }
//             if (nelem == 0)
//                 break; // exit the while loop if we had too much extra capacity (cap1)
//             int ind = rand() % nelem;
//             input[inds[ind]]--;
//             cap1--;
//         }
//     }
//
//
//     for (int i = 0; i < input.size(); i++)
//         sent[i] -= input[i];
//
//     return sent; // return what was sent
//
//     //return input; // return what's left
// }
//
// std::vector<std::vector<int>> RlbMaster::fairshare2d(std::vector<std::vector<int>> input, std::vector<int> cap0, std::vector<int> cap1) {
//
//     // if we take `input` as an N x M matrix (N rows, M columns)
//     // then cap0[i] is the capacity of the sum of the i-th row
//     // and cap1[i] is the capacity of the sum of the i-th column
//
//     // build output
//     std::vector<std::vector<int>> sent;
//     sent.resize(input.size());
//     for (int i = 0; i < input.size(); i++) {
//         sent[i].resize(input[0].size());
//         for (int j=0; j < input[0].size(); j++)
//             sent[i][j] = 0;
//     }
//
//     int maxiter = 5;
//     int iter = 0;
//
//     int nelem = 0;
//     for (int i = 0; i < input.size(); i++)
//         for (int j=0; j < input[0].size(); j++)
//             if (input[i][j] > 0)
//                 nelem++;
//
//     while (nelem != 0 && iter < maxiter) {
//
//         // temporary matrix:
//         std::vector<std::vector<int>> sent_temp;
//         sent_temp.resize(input.size());
//         for (int i = 0; i < input.size(); i++) {
//             sent_temp[i].resize(input[0].size());
//             for (int j=0; j < input[0].size(); j++)
//                 sent_temp[i][j] = 0;
//         }
//
//         // sweep rows (i): (cols j)
//         for (int i = 0; i < input.size(); i++) {
//             int prev_alloc = 0;
//             for (int j = 0; j < input[0].size(); j++)
//                 prev_alloc += sent[i][j];
//             sent_temp[i] = fairshare1d(input[i], cap0[i] - prev_alloc, true);
//         }
//
//         // sweep columns (i): (rows j)
//         for (int i = 0; i < input[0].size(); i++) {
//             int prev_alloc = 0;
//             std::vector<int> temp_vect;
//             for (int j = 0; j < input.size(); j++) {
//                 prev_alloc += sent[j][i];
//                 temp_vect.push_back(sent_temp[j][i]);
//             }
//             temp_vect = fairshare1d(temp_vect, cap1[i] - prev_alloc, true);
//             for (int j = 0; j < input.size(); j++)
//                 sent_temp[j][i] = temp_vect[j];
//         }
//
//
//         // update the `sent` matrix with the `sent_temp` matrix:
//         for (int i = 0; i < input.size(); i++)
//             for (int j=0; j < input[0].size(); j++)
//                 sent[i][j] += sent_temp[i][j];
//
//         // update the input matrix:
//         for (int i = 0; i < input.size(); i++) {
//             for (int j=0; j < input[0].size(); j++) {
//                 input[i][j] -= sent_temp[i][j];
//                 if (input[i][j] < 0)
//                     input[i][j] = 0;
//             }
//         }
//
//         // work our way "backwards", checking if cap1[] and cap0[] have been used up
//
//         // cap1[] used up? if so, set the column to zero
//         // (sweep columns = i)
//         for (int i = 0; i < input[0].size(); i++) {
//             int remain = cap1[i];
//             for (int j = 0; j < input.size(); j++)
//                 remain -= sent[j][i];
//
//             if (remain <= 0) {
//                 for (int j = 0; j < input.size(); j++)
//                     input[j][i] = 0;
//             }
//         }
//
//         // cap0[] used up? if so, set the row to zero
//         // (sweep rows = i)
//         for (int i = 0; i < input.size(); i++) {
//             int remain = cap0[i];
//             for (int j = 0; j < input[0].size(); j++)
//                 remain -= sent[i][j];
//
//             if (remain <= 0) {
//                 for (int j = 0; j < input.size(); j++)
//                     input[i][j] = 0;
//             }
//         }
//
//         // get number of remaining elements:
//         nelem = 0;
//         for (int i = 0; i < input.size(); i++)
//             for (int j=0; j < input[0].size(); j++)
//                 if (input[i][j] > 0)
//                     nelem++;
//
//         iter++;
//     }
//
//     return sent; // return what was sent
//
// }
//
// std::vector<std::vector<int>> RlbMaster::fairshare2d_2(std::vector<std::vector<int>> input, int cap0, std::vector<int> cap1) {
//
//     // if we take `input` as an N x M matrix (N rows, M columns)
//     // then cap0 is the capacity shared across all elements
//     // and cap1[i] is the capacity of the sum of the i-th column
//
//     // build output
//     std::vector<std::vector<int>> sent;
//     sent.resize(input.size());
//     for (int i = 0; i < input.size(); i++) {
//         sent[i].resize(input[0].size());
//         for (int j=0; j < input[0].size(); j++)
//             sent[i][j] = 0;
//     }
//
//     int maxiter = 5;
//     int iter = 0;
//
//     int nelem = 0;
//     for (int i = 0; i < input.size(); i++)
//         for (int j=0; j < input[0].size(); j++)
//             if (input[i][j] > 0)
//                 nelem++;
//
//     while (nelem != 0 && iter < maxiter) {
//
//         int prev_alloc;
//
//         // temporary matrix:
//         std::vector<std::vector<int>> sent_temp;
//         sent_temp.resize(input.size());
//         for (int i = 0; i < input.size(); i++) {
//             sent_temp[i].resize(input[0].size());
//             for (int j=0; j < input[0].size(); j++)
//                 sent_temp[i][j] = 0;
//         }
//
//         // sweep all elements
//         // sum prev alloc & make into 1d vector
//         std::vector<int> temp_all;
//         temp_all.resize(input.size() * input[0].size());
//         int cnt = 0;
//         prev_alloc = 0;
//
//         for (int i = 0; i < input.size(); i++)
//             for (int j = 0; j < input[0].size(); j++) {
//                 prev_alloc += sent[i][j];
//                 temp_all[cnt] = input[i][j];
//                 cnt++;
//             }
//         temp_all = fairshare1d(temp_all, cap0 - prev_alloc, true);
//         cnt = 0;
//         for (int i = 0; i < input.size(); i++)
//             for (int j = 0; j < input[0].size(); j++) {
//                 sent_temp[i][j] = temp_all[cnt];
//                 cnt++;
//             }
//
//
//         // sweep columns (i): (rows j)
//         for (int i = 0; i < input[0].size(); i++) {
//             prev_alloc = 0;
//             std::vector<int> temp_vect;
//             for (int j = 0; j < input.size(); j++) {
//                 prev_alloc += sent[j][i];
//                 temp_vect.push_back(sent_temp[j][i]);
//             }
//             temp_vect = fairshare1d(temp_vect, cap1[i] - prev_alloc, true);
//             for (int j = 0; j < input.size(); j++)
//                 sent_temp[j][i] = temp_vect[j];
//         }
//
//
//         // update the `sent` matrix with the `sent_temp` matrix:
//         for (int i = 0; i < input.size(); i++)
//             for (int j=0; j < input[0].size(); j++)
//                 sent[i][j] += sent_temp[i][j];
//
//         // update the input matrix:
//         for (int i = 0; i < input.size(); i++) {
//             for (int j=0; j < input[0].size(); j++) {
//                 input[i][j] -= sent_temp[i][j];
//                 if (input[i][j] < 0)
//                     input[i][j] = 0;
//             }
//         }
//
//         // work our way "backwards", checking if cap1[] and cap0 have been used up
//
//         // cap1[] used up? if so, set the column to zero
//         // (sweep columns = i)
//         for (int i = 0; i < input[0].size(); i++) {
//             int remain = cap1[i];
//             for (int j = 0; j < input.size(); j++)
//                 remain -= sent[j][i];
//
//             if (remain <= 0) {
//                 for (int j = 0; j < input.size(); j++)
//                     input[j][i] = 0;
//             }
//         }
//
//         // cap0 used up? if so, break (i.e. set the entire matrix to zero)
//         int remain = cap0;
//         for (int i = 0; i < input.size(); i++)
//             for (int j = 0; j < input[0].size(); j++)
//                 remain -= sent[i][j];
//         if (remain <= 0)
//             break;
//
//         // get number of remaining elements:
//         nelem = 0;
//         for (int i = 0; i < input.size(); i++)
//             for (int j=0; j < input[0].size(); j++)
//                 if (input[i][j] > 0)
//                     nelem++;
//
//         iter++;
//     }
//
//     return sent; // return what was sent
//
// }



auto owned_ports(const node_t node) {
    return views::iota(0, network.parameters.num_ports()) | views::filter([node](const port_t port){ return network.topology.owner(port) == node; });
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
    [[nodiscard]] const packet_t& operator()(flow_t flow) const { return traffic(network.flows[flow].ingress, network.flows[flow].egress); }
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
                // traffic(node, target) = 0;
                assert(offer.capacity >= 0);
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

    [[nodiscard]] SchedulerChoice get_choice(flow_t flow, const std::vector<std::vector<Offer>>& offers) const {
        return get_choice(network.flows[flow].ingress, network.flows[flow].egress, offers);
    }
    [[nodiscard]] SchedulerChoice get_choice(node_t source, node_t destination, const std::vector<std::vector<Offer>>& offers) const {
        SchedulerChoice scheduler_choice;
        for (const auto& [target, port] : targets()) {
            if (target == destination) { // Direct traffic
                auto offer = std::ranges::find(offers[local_], target, [](const auto& offer){ return offer.target; });
                assert(offer != offers[local_].end());
                if (source != local_) {
                    // 1st priority: Non-local direct traffic
                    // offers[local_][target]
                    scheduler_choice.emplace_back(port, traffic(source, target));
                    // return SchedulerChoice(port, );
                } else {
                    // 2nd priority: Local direct traffic. TODO: if no more capacity, return -1;
                    if (offer->capacity >= 0) {
                        scheduler_choice.emplace_back(port, local_traffic(target));
                    } else if (local_traffic(target) + offer->capacity >= 0) {
                        scheduler_choice.emplace_back(port, local_traffic(target) + offer->capacity);
                    }
                    // return SchedulerChoice(port);
                }
            }
        }
        if (source == local_) {
            // TODO: 3rd priority: Local indirect traffic, if offer was accepted by target.
            for (const auto& [target, port] : targets()) {
                if (target != destination) {
                    auto offer = std::ranges::find(offers[local_], target, [](const auto& offer){ return offer.target; });
                    if (offer->capacity) {
                        scheduler_choice.emplace_back(port, offer->offer[destination]);
                    }
                }
            }
        }
        // TODO: Simulation must allow not immediately scheduling for future ports, but wait and see which port is good to choose.
        return scheduler_choice;
        // return -1; // Don't schedule yet.
        // return targets()[random_num%targets().size()].second;  // Random target for now...
    }

private:
    node_t n_nodes_ = 0;
    std::vector<packet_t> table_;
    node_t local_ = 0;
    std::vector<std::pair<node_t,port_t>> targets_;  // (node,port) \in targets: In current phase, we can send traffic to node through port.
};

void compute_rotor_lb(phase_t phase_i) {
    // RlbMaster rlb(phase_i);
    // for (int i = 0; i < network.parameters.num_switches(); i++) {
    //     switch_t sw = rlb._current_commit_queue;
    //     rlb.newMatching();
    //     for (int crtToR = 0; crtToR < rlb._N; crtToR++) {
    //         // get the list of new dst hosts (that every host in this rack is now connected to)
    //         int dstToR = network.topology(phase_i, network.parameters.port_of(crtToR, sw));  // _top->get_nextToR(slice, crtToR, _current_commit_queue + _hpr);
    //         if (crtToR != dstToR) {
    //             std::vector<int> src_hosts;
    //             int basehost = crtToR * rlb._hpr;
    //             for (int j = 0; j < rlb._hpr; j++) {
    //                 src_hosts.push_back(basehost + j);
    //             }
    //             for (int j = 0; j < rlb._hpr; j++) {
    //                 int src_host = basehost + j;
    //                 if (rlb._Nsenders[src_host] > 0) {
    //                     for (int sender = 0; sender < rlb._Nsenders[src_host]; sender++) {
    //                         auto dest = rlb._dst_labels[src_host][sender];
    //                         auto packets = rlb._pkts_to_send[src_host][sender];
    //                         // sw
    //                         // _commits.emplace_back();
    //                     }
    //                 }
    //             }
    //         }
    //     }
    // }


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
                packet_t load = PortLoad::getPacketsForFlow(node, flow);
                table(flow) = load;
            }
        }
        offers.emplace_back(table.get_offer());
    }

    for (const node_t node : views::iota(0, params.num_nodes)) {
        for (const flow_t flow : views::iota(0, params.num_flows)) {
            (*pChoiceCache)[{node, flow}] = tables[node].get_choice(flow, offers);
        }
    }
}

static packet_t cachedChoice(const node_t node, const flow_t flow, const phase_t phase_i, const switch_t sw) {
    const port_t port = network.parameters.port_of(node, sw);
    auto key = ChoiceArgs{node, flow};
    auto iter = pChoiceCache->find(key);
    if (iter == pChoiceCache->end()) {
        compute_rotor_lb(phase_i);
        iter = pChoiceCache->find(key);
    }
    auto& choice = iter->second;
    auto port_choice = std::ranges::find(choice, port, [](const auto& pw){ return pw.port; });
    return port_choice == choice.end() ? 0 : port_choice->weight;
}

// local data per destination
// non-local data per source and destination
// = table per node of traffic enqueued per (source,destination)-pair except diagonal and self-destination.

void customGetScheduleChoice(node_t node, flow_t flow, phase_t phase_i, switch_t sw, packet_t& choice_weight) {
    choice_weight = cachedChoice(node, flow, phase_i, sw);
}

void customPrepareChoices() {
    random_num = random_gen();
    pChoiceCache->clear();
}
void scheduler_init() {
    if (!pChoiceCache) {
        // readEnvVars();
        pChoiceCache = std::make_unique<std::unordered_map<ChoiceArgs, SchedulerChoice>>();
    }
    random_gen = std::mt19937(123456);
}
