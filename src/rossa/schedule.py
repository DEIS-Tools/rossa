from functools import cache
from random import Random
from typing import Union
import igraph as ig
from model import Flow, Schedule, ScheduleChoice, Model, Node, Port

__all__ = [
    "ConnectivityGraph",
    "QuickestMustHopSchedule",
    "QuickestSchedule",
    "MinimizeHopsSchedule",
    "ExactlyTwoHopsMayWaitSchedule",
]


class QuickestMustHopSchedule:
    def __init__(self, **kwargs):
        # Needs no config entries
        pass

    def make_schedule(self, model: Model, random: Random) -> Schedule:
        helper_graph = ConnectivityGraph(model, random)
        schedule = helper_graph.fewest_hops_must_hop_schedule()
        return schedule


class QuickestSchedule:
    def __init__(self, **kwargs):
        # Needs no config entries
        pass

    def make_schedule(self, model: Model, random: Random) -> Schedule:
        helper_graph = ConnectivityGraph(model, random)
        schedule = helper_graph.fastest_schedule()
        return schedule


class MinimizeHopsSchedule:
    def __init__(self, **kwargs):
        # Needs no config entries
        pass

    def make_schedule(self, model: Model, random: Random) -> Schedule:
        helper_graph = ConnectivityGraph(model, random)
        schedule = helper_graph.minimize_hops()
        return schedule


class ExactlyTwoHopsMayWaitSchedule:
    def __init__(self, **kwargs) -> None:
        pass

    def make_schedule(self, model: Model, random: Random) -> Schedule:
        helper_graph = ConnectivityGraph(model, random)
        schedule = helper_graph.exactly_two_hops_may_wait()
        return schedule


class ConnectivityGraph:
    """
    Construct a graph that for each phase has
    - a node-vertex for each node in the model
    - a port-vertex for each port in the model

    We can then, depending on the model:

    - Connect each port-vertex to a node-vertex and each node-vertex to a port-vertex for a future phase.

    Then we solve a graph problem to derive a schedule.

    Implementation Notes:
        The phases of ports are the phases packets are sent in by the scheduler.
        We store in the 'data' attributes the (phase, index of node/port) pair in the Rotor topology.
    """

    def __init__(self, model: Model, random: Random):
        self.model: Model = model
        self.graph: ig.Graph
        self.random: Random = random
        self._num_nverts = model.num_phases * model.num_nodes
        self._num_pverts = model.num_phases * model.num_ports

    def _index_of_node_vertex(self, phase, node: Node):
        return phase * self.model.num_nodes + node.index

    def _index_of_port_vertex(self, phase, port: Port):
        return self._num_nverts + phase * self.model.num_ports + port.index

    def _is_port_vertex(self, index):
        return index >= self._num_nverts and index < (
            self._num_nverts + self._num_pverts
        )

    def _is_node_vertex(self, index):
        return not self._is_port_vertex(index)

    def _get_node_vertex_name(self, phase, node: Node):
        return f"N[{node.index}]_{phase}"

    def _get_port_vertex_name(self, phase, port: Port):
        return f"P[{port.index}]_{phase}"

    def _phase_shift(self, phase, amount):
        if amount > self.model.num_phases:
            raise "Check this logic if you are shifting that high number of phases"
        return (phase + amount) % self.model.num_phases

    def _add_port_to_nodes(self, graph, cost=1, phase_shift=0):
        model = self.model
        to_add = []
        for from_phase, target_node_indices in enumerate(model.topology):
            for from_port_index, target_node_index in enumerate(target_node_indices):
                from_port = model.ports[from_port_index]
                to_node = model.nodes[target_node_index]
                from_graph_node = self._index_of_port_vertex(from_phase, from_port)
                target_phase = self._phase_shift(from_phase, phase_shift)
                to_graph_node = self._index_of_node_vertex(target_phase, to_node)
                to_add.append((from_graph_node, to_graph_node))
        graph.add_edges(to_add, {"weight": [cost] * len(to_add)})

    def _add_nodes_to_port(self, graph, cost=0, phase_shift=1):
        model = self.model
        to_add = []
        for from_phase in range(model.num_phases):
            for node in model.nodes:
                from_graph_node = self._index_of_node_vertex(from_phase, node)
                target_phase = self._phase_shift(from_phase, phase_shift)
                for port in model.ports_of_node(node):
                    to_graph_node = self._index_of_port_vertex(target_phase, port)
                    to_add.append((from_graph_node, to_graph_node))

        graph.add_edges(to_add, {"weight": [cost] * len(to_add)})

    def _add_port_to_same_future_port(self, graph, cost=1, phase_shift=1):
        model = self.model
        to_add = []
        for from_phase in range(model.num_phases):
            for port in model.ports:
                from_graph_port = self._index_of_port_vertex(from_phase, port)
                target_phase = self._phase_shift(from_phase, phase_shift)
                to_graph_port = self._index_of_port_vertex(target_phase, port)
                to_add.append((from_graph_port, to_graph_port))
        graph.add_edges(to_add, {"weight": [cost] * len(to_add)})

    def _construct_nodes(self):
        model = self.model
        node_ids = [(i, n) for i in range(model.num_phases) for n in model.nodes] + [
            (i, p) for i in range(model.num_phases) for p in model.ports
        ]
        node_vert_names = [
            self._get_node_vertex_name(i, n)
            for i in range(model.num_phases)
            for n in model.nodes
        ]
        node_port_names = [
            self._get_port_vertex_name(i, p)
            for i in range(model.num_phases)
            for p in model.ports
        ]

        self.graph = g = ig.Graph(
            n=(self._num_nverts + self._num_pverts),
            directed=True,
        )
        g.vs["label"] = node_vert_names + node_port_names
        g.vs["data"] = node_ids
        return g

    def _shortest_path_for_flow(
        self, egress: Node
    ) -> dict[tuple[int, Node], list[tuple[int, Union[Node, Port]]]]:
        """Computes shortest path to any vertice representing the egress node in any phase
        from all node vertices and node ports.

        Returns a dictionary of
            (phase, node/port): [nodeport_graph1, nodeport_graph2, ...]
        """
        model = self.model
        graph = self.graph
        # Only the vertices corresponding to nodes.
        node_graph_vertices = [
            self._index_of_node_vertex(pi, n)
            for pi in range(model.num_phases)
            for n in model.nodes
        ]
        best_paths = {}
        # Compute for each phase, path from all nodes to the destination node in a given phase
        # We replace the path from any given source node if it has better cost to the node in this phase
        for phase in range(model.num_phases):
            dest_node = self._index_of_node_vertex(phase, model.nodes[egress.index])
            paths = graph.get_shortest_paths(
                v=dest_node,
                mode="in",
                output="epath",
                to=node_graph_vertices,
                # weights=graph.es['weight']
            )
            for g_node, path in zip(node_graph_vertices, paths):
                # Because mode='in' path is reversed
                path_cost = sum(graph.es[e]["weight"] for e in path)
                # Convert path to node_ids in forward order.
                vert_ids = [graph.es[e].source for e in reversed(path)] + [dest_node]
                in_forward_order = [graph.vs[n]["data"] for n in vert_ids]

                key: tuple[int, Node] = graph.vs[g_node]["data"]
                # Verify last node is actually egress node.
                if in_forward_order[-1][1] != egress:
                    raise Exception("No path found", in_forward_order[-1][1], egress)
                # Check if better result.
                if key not in best_paths:
                    best_paths[key] = (in_forward_order, path_cost)
                elif key in best_paths and (best_paths[key][1] > path_cost):
                    best_paths[key] = (in_forward_order, path_cost)
        return {k: v[0] for k, v in best_paths.items()}

    def _path_to_ports(
        self, paths, *, collapse_port_waits=True
    ) -> dict[tuple[int, int], ScheduleChoice]:
        node_to_port = {}
        for (from_phase, from_node), path in paths.items():
            if len(path) > 1:
                to_phase, port = path[1]
                assert isinstance(port, Port)
                if collapse_port_waits:
                    for i in range(2, len(path)):
                        _, next_port = path[i]
                        if not isinstance(next_port, Port):
                            break
                        # We must always be at the same port, if we wait phases
                        assert next_port == port
                    # A path must always lead to a node
                    assert i < len(path)
                    # We send in that nodes phase
                    target_phase, target_node = path[i]
                node_to_port[(from_phase, from_node)] = ScheduleChoice(
                    port, target_phase
                )
            else:
                # Does not matter really
                assert len(path) == 1
                node_to_port[(from_phase, from_node)] = ScheduleChoice(None, None)
        return node_to_port

    def _ports_to_choices(
        self, flow_port_choices: list[dict[tuple[int, int], ScheduleChoice]]
    ) -> Schedule:
        model = self.model
        num_phases = model.num_phases
        schedule = Schedule(model, num_phases=num_phases)
        for cur_phase in range(num_phases):
            for node_index, node in enumerate(model.nodes):
                assert node_index == node.index
                for _flow_index, port_choices in enumerate(flow_port_choices):
                    (target_port, target_phase) = port_choices[(cur_phase, node)]
                    if target_phase is None:
                        # Then send in next
                        target_phase = (cur_phase + 1) % num_phases
                    if target_port is None:
                        # Pick any port if it does not matter.
                        target_port = model.ports_of_node(node)[0]
                    choice = ScheduleChoice(target_port.index, target_phase)
                    schedule.set(
                        phase=cur_phase,
                        node=node.index,
                        flow=_flow_index,
                        choice=choice,
                    )
        return schedule

    def paths_for_flows(self, flows: list[Flow]):
        @cache
        def get_for_flow(egress: Node):
            return self._shortest_path_for_flow(egress)

        result = [dict(get_for_flow(f.egress)) for f in flows]
        return result

    def fewest_hops_must_hop_schedule(self) -> Schedule:
        """Warning: modifies graph permanently.#2"""
        self._construct_nodes()

        self._add_port_to_nodes(self.graph)
        self._add_nodes_to_port(self.graph)

        self.graph.es["label"] = self.graph.es["weight"]
        # ig.plot(self.graph, target='myfile.pdf', )

        flow_paths = self.paths_for_flows(self.model.flows)
        node_to_ports = [self._path_to_ports(paths) for paths in flow_paths]
        sched = self._ports_to_choices(node_to_ports)
        if not sched.all_hop_immediately():
            print("WARNING: Someone is unable to hop immediately")
        return sched

    def fastest_schedule(self) -> Schedule:
        """Warning: modifies graph permanently. #3"""
        self._construct_nodes()

        self._add_port_to_nodes(self.graph)
        self._add_nodes_to_port(self.graph)
        # Can stay at port
        self._add_port_to_same_future_port(self.graph)

        flow_paths = self.paths_for_flows(self.model.flows)
        node_to_ports = [self._path_to_ports(paths) for paths in flow_paths]
        sched = self._ports_to_choices(node_to_ports)
        return sched

    def minimize_hops(self) -> Schedule:
        """Warning: modifies graph permanently.  #1"""
        self._construct_nodes()

        self._add_port_to_nodes(self.graph, cost=1_000_000)
        self._add_nodes_to_port(self.graph)
        # Can stay at port
        self._add_port_to_same_future_port(self.graph, cost=0)

        flow_paths = self.paths_for_flows(self.model.flows)
        node_to_ports = [self._path_to_ports(paths) for paths in flow_paths]
        sched = self._ports_to_choices(node_to_ports)
        return sched

    def exactly_two_hops_may_wait(self, hops=2) -> Schedule:
        model = self.model
        graph = construct_emulated_node_graph(model)
        rnd = self.random
        # Randomly select simple paths with exactly two edges.
        flow_node_paths = []
        for flow in self.model.flows:
            all_paths = graph.get_all_simple_paths(
                v=flow.ingress.index, to=flow.egress.index, cutoff=hops
            )
            valid_paths = [p for p in all_paths if len(p) == (hops + 1)]
            assert len(valid_paths) >= 1
            flow_node_paths.append(rnd.choice(valid_paths))
        # For all phases, using the paths and the topology select the forthcoming connecting phases to transmit.
        # We gather the schedule choices
        schedule = Schedule(model, num_phases=model.num_phases)

        def port_connecting(model: Model, phase, nf: int, nt: int):
            owned_ports = model.ports_of_node(nf)
            return [p for p in owned_ports if model.topology[phase][p.index] == nt]

        def expand_path(model: Model, flow_node_path, flow_index):
            for phase in range(model.num_phases):
                for nf, nt in zip(flow_node_path[:-1], flow_node_path[1:]):
                    found = False
                    for waits in range(1, model.num_phases):
                        future_phase = model.mod_add_phase(phase, waits)
                        connecting_ports = port_connecting(model, future_phase, nf, nt)
                        if connecting_ports:
                            port = rnd.choice(connecting_ports)
                            found = True
                            schedule.set(
                                phase=phase,
                                node=nf,
                                flow=flow_index,
                                choice=ScheduleChoice(port.index, future_phase),
                            )
                            break
                    if not found:
                        raise Exception("No port found for two hop path")

        for flow_index, flow_path in enumerate(flow_node_paths):
            expand_path(model, flow_path, flow_index)

        return schedule


def construct_emulated_node_graph(model: Model) -> ig.Graph:
    num_nodes = model.num_nodes
    g = ig.Graph(n=num_nodes, directed=True)
    edges_to_add = []
    # Connect nodes if any phase connects a node's port with the target node.
    for _from_phase, target_node_indices in enumerate(model.topology):
        for from_port_index, target_node_index in enumerate(target_node_indices):
            from_port = model.ports[from_port_index]
            from_node = from_port.owner
            edges_to_add.append((from_node.index, target_node_index))
    g.add_edges(edges_to_add)
    return g
