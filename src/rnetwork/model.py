from collections import namedtuple, defaultdict
from typing import Any, Optional, Sequence, NamedTuple

__all__ = ["Flow", "ScheduleChoice", "Node", "Port", "RotatingSwitches"]

ScheduleChoice = namedtuple("ScheduleChoice", ["port", "phase"])
Node = namedtuple("Node", ["index"])


class Flow(NamedTuple):
    ingress: Node
    egress: Node
    amount: int


class Port(NamedTuple):
    index: int
    owner: Node
    capacity: int
    bandwidth: int


class Model:
    def __init__(self) -> None:
        self.nodes: list[Node] = []
        self.ports: list[Port] = []
        self.flows: list[Flow] = []
        self._topology: Any = None

    def add_node(self) -> Node:
        index = len(self.nodes)
        node = Node(index)
        self.nodes.append(node)
        return node

    def add_port(self, owner, capacity, bandwidth) -> Port:
        index = len(self.ports)
        port = Port(index, owner, capacity, bandwidth)
        self.ports.append(port)
        return port

    def ports_of_node(self, node: "Node | int") -> Sequence[Port]:
        if type(node) == int:
            node = self.nodes[node]
        return [p for p in self.ports if p.owner == node]

    def add_flow(self, flow):
        self.flows.append(flow)

    @property
    def num_nodes(self):
        return len(self.nodes)

    @property
    def num_ports(self):
        return len(self.ports)

    @property
    def num_flows(self):
        return len(self.flows)

    @property
    def topology(self) -> Sequence[Sequence[int]]:
        """
        topology[phase][port] = target_node
        """
        return self._topology

    @property
    def num_phases(self):
        return len(self.topology)

    def mod_add_phase(self, phase, add=1):
        return (phase + add) % self.num_phases

    @topology.setter
    def topology(self, value):
        self._topology = value


class Schedule(defaultdict):
    def __init__(self, model: Model, num_phases):
        self._num_nodes = model.num_nodes
        self._num_phases = num_phases
        self._num_flows = model.num_flows
        self._model = model

    def __missing__(self, key):
        # Missing entry.
        # Select any owned port and forward next phase.
        phase, node_idx, _flow = key
        return ScheduleChoice(
            self._model.ports_of_node(node_idx)[0].index,
            self._model.mod_add_phase(phase, add=1),
        )

    def set(self, phase: int, node: int, flow: int, choice: ScheduleChoice):
        self[(phase, node, flow)] = choice

    def get(self, phase: int, node: int, flow: int):
        return self.get(
            (phase, node, flow),
        )

    def _table_iter(self):
        return (
            (i, n, f)
            for i in range(self._num_phases)
            for n in range(self._num_nodes)
            for f in range(self._num_flows)
        )

    def is_complete(self) -> bool:
        return not any(self[x] is None for x in self)

    def all_hop_immediately(self) -> bool:
        phases = self._num_phases
        for entry, choice in self.items():
            (
                phase_i,
                _,
                _,
            ) = entry
            (_, phase_j) = choice
            next_i = (phase_i + 1) % phases
            if next_i != phase_j:
                print(phases, entry, choice)
                return False
        return True

    def to_lists(self) -> list[list[list[Optional[ScheduleChoice]]]]:
        return [
            [
                [self[(i, n, f)] for f in range(self._num_flows)]
                for n in range(self._num_nodes)
            ]
            for i in range(self._num_phases)
        ]


class RotatingSwitches:
    def __init__(self, num_switches, interval, start_offset, **kwargs):
        self.num_switches = num_switches
        self.interval = interval
        self.start_offset = start_offset
        self.initial_offsets = tuple(
            (i * self.interval) + self.start_offset for i in range(self.num_switches)
        )
        if self.num_switches != len(self.initial_offsets):
            raise RuntimeError("Initial offsets must match number of switches")

    def get_topology(self, model: Model) -> Sequence[Sequence[int]]:
        num_nodes = model.num_nodes
        seen = set()  # For offset cycle detection.
        topology = []
        offsets = tuple(self.initial_offsets)

        def target(source, offset):
            t = source + offset
            # Wrap around
            if t >= num_nodes:
                t -= num_nodes
            # Skip self
            if t == source:
                t += 1
            return t

        def advance_offset(offset, amount=1):
            return ((offset - 1) + amount) % (num_nodes - 1) + 1

        while offsets not in seen:
            seen.add(offsets)
            matching = [
                # Select target for each port for each node.
                target(n, i)
                for n in range(num_nodes)
                for i in offsets
            ]
            topology.append(matching)
            offsets = tuple(advance_offset(x) for x in offsets)

        return topology
