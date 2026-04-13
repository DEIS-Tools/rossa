from collections import namedtuple, defaultdict
from typing import Any, Optional, Sequence, NamedTuple
import json
import os

__all__ = ["Flow", "ScheduleChoice", "Node", "Port", "RotatingSwitches", "Rotornet2024Switches", "RotorNetLowLatency", "FileTopologyBuilder"]

ScheduleChoice = namedtuple("ScheduleChoice", ["port", "phase"])

class Node(NamedTuple):
    index: int
    capacity: int
class Flow(NamedTuple):
    ingress: Node
    egress: Node
    amount_over_time: list[int]  # Each element is the amount at that timestep. Cycles as needed for longer simulations, so constant amount is just a singleton list.

class Port(NamedTuple):
    index: int
    owner: Node
    bandwidth: int


class Model:
    def __init__(self) -> None:
        self.nodes: list[Node] = []
        self.ports: list[Port] = []
        self.flows: list[Flow] = []
        self._topology: Any = None

    def add_node(self, capacity) -> Node:
        index = len(self.nodes)
        node = Node(index, capacity)
        self.nodes.append(node)
        return node

    def add_port(self, owner, bandwidth) -> Port:
        index = len(self.ports)
        port = Port(index, owner, bandwidth)
        self.ports.append(port)
        return port

    def ports_of_node(self, node: "Node | int") -> Sequence[Port]:
        if type(node) == int:
            node = self.nodes[node]
        return [p for p in self.ports if p.owner == node]

    def add_flow(self, flow: Flow):
        self.flows.append(flow)
    def add_flows(self, flows: list[Flow]):
        self.flows.extend(flows)
    
    def get_max_flow_time(self) -> int:
        if len(self.flows) == 0: return 0
        length = len(self.flows[0].amount_over_time)
        assert(all(length == len(flow.amount_over_time) for flow in self.flows))
        return length


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
    def num_switches(self):
        return self.num_ports // self.num_nodes

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

    def to_json_file_format(self):
        return [[[self.topology[phase][switch + node * self.num_switches] for node in range(self.num_nodes)] for switch in range(self.num_switches)] for phase in range(self.num_phases)]


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


class Rotornet2024Switches:
    def __init__(self, num_switches, **kwargs):
        self.num_switches = num_switches
        assert(num_switches == 4)

    def get_topology(self, model: Model) -> Sequence[Sequence[int]]:
        num_nodes = model.num_nodes
        assert(num_nodes == 16)
        
        switch0 = [[1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14],
                    [3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12],
                    [7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8],
                    [15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0]]
        switch1 = [[4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11],
                    [6, 7, 4, 5, 2, 3, 0, 1, 14, 15, 12, 13, 10, 11, 8, 9],
                    [2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13],
                    [10, 11, 8, 9, 14, 15, 12, 13, 2, 3, 0, 1, 6, 7, 4, 5]]
        switch2 = [list(reversed(phase)) for phase in switch1]
        switch3 = [list(reversed(phase)) for phase in switch0]
        switches = [switch0, switch1, switch2, switch3] 
        
        topology = []
        num_phases = 4
        for phase in range(num_phases):
            matching = [
                # Select target for each port for each node.
                switches[switch][phase][node]
                for node in range(num_nodes)
                for switch in range(self.num_switches)
            ]
            topology.append(matching)
        return topology

class RotorNetLowLatency:
    def __init__(self, num_switches, **kwargs):
        self.num_switches = num_switches
        assert(num_switches == 4)

    def get_topology(self, model: Model) -> Sequence[Sequence[int]]:
        num_nodes = model.num_nodes
        assert(num_nodes == 16)
        base_r = {0: [ 1,  3,  7, 15],
                  1: [ 0,  2,  6, 14], 
                  2: [ 3,  1,  5, 13], 
                  3: [ 2,  0,  4, 12], 
                  4: [ 5,  7,  3, 11], 
                  5: [ 4,  6,  2, 10], 
                  6: [ 7,  5,  1,  9], 
                  7: [ 6,  4,  0,  8], 
                  8: [ 9, 11, 15,  7], 
                  9: [ 8, 10, 14,  6], 
                 10: [11,  9, 13,  5], 
                 11: [10,  8, 12,  4], 
                 12: [13, 15, 11,  3], 
                 13: [12, 14, 10,  2], 
                 14: [15, 13,  9,  1], 
                 15: [14, 12,  8,  0]}
        gold_perm=[ 0,10, 2, 8, 4,14, 6,12, 1,11, 3, 9, 5,15, 7,13]
        r1_perm = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15]
        r2_perm = [ 5, 4, 7, 6, 1, 0, 3, 2,13,12,15,14, 9, 8,11,10]
        r3_perm = list(reversed(r2_perm))
        r4_perm = list(reversed(r1_perm))
        perms = [r1_perm, r2_perm, r3_perm, r4_perm]
        topology = []
        num_phases = 4
        for phase in range(num_phases):
            matching = [
                # Select target for each port for each node.
                base_r[perms[switch][gold_perm[node]]][phase]
                for node in range(num_nodes)
                for switch in range(self.num_switches)
            ]
            topology.append(matching)
        return topology

class FileTopologyBuilder:
    def __init__(self, num_switches, topology_file):
        self.num_switches = num_switches
        assert(os.path.isfile(topology_file))
        self.file_path = topology_file
    
    def get_topology(self, model: Model) -> Sequence[Sequence[int]]:
        num_nodes = model.num_nodes
        topology = []
        with open(self.file_path, "r") as file:
            topo_json = json.load(file)
            for phase_json in topo_json:
                assert(len(phase_json) == self.num_switches)
                matching = [
                    # Select target for each port for each node.
                    phase_json[switch][node]
                    for node in range(num_nodes)
                    for switch in range(self.num_switches)
                ]
                topology.append(matching)
        return topology


def topo_to_pairings(model: Model):
    num_switches = model.num_ports // model.num_nodes
    for phase, matching in enumerate(model.topology):
        for src_node in range(model.num_nodes):
            for switch in range(num_switches):
                dst_node = matching[src_node * num_switches + switch]
                yield (switch, src_node, dst_node, phase)

def print_topology_by_switch(model: Model):
    shaded = True

    phases = "░▒▓█EFGHIJKLMNOP" if shaded else "ABCDEFGHIJKLMNOP"
    # phases = range(1,20)
    num_switches = model.num_ports // model.num_nodes
    for switch in range(num_switches):
        print("Rotor:", switch+1)
        points = iter(sorted(filter(lambda x: x[0] == switch, topo_to_pairings(model))))
        (_, src, dst, phase) = next(points)
        for row in range(model.num_nodes):
            for col in range(model.num_nodes):
                if row == src and col == dst:
                    print(f'{phases[phase]}{phases[phase]}' if shaded else f'  {phases[phase]}', end='')
                    try:
                        (_, src, dst, phase) = next(points)
                    except StopIteration:
                        pass
                else:
                    print('▏▔' if shaded else '  .', end='')
            print('▏' if shaded else '')
        print('▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔' if shaded else '')

def print_topology_by_phase(model: Model):
    phases = "ABCDEFGHIJKLMNOP"
    for phase in range(model.num_phases):
        print("Phase:", phases[phase])
        points = iter(sorted(filter(lambda x: x[3] == phase, topo_to_pairings(model)), key=lambda x: (x[1], x[2])))
        (switch, src, dst, _) = next(points)
        for row in range(model.num_nodes):
            for col in range(model.num_nodes):
                if row == src and col == dst:
                    print(f'{switch+1:3d}', end='')
                    try:
                        (switch, src, dst, _) = next(points)
                    except StopIteration:
                        pass
                else:
                    print('  .', end='')
            print()
        print()

def print_topology_combined(model: Model):
    phases = "ABCDEFGHIJKLMNOP"
    points = iter(sorted(topo_to_pairings(model), key=lambda x: (x[1], x[2])))
    (switch, src, dst, phase) = next(points)
    for row in range(model.num_nodes):
        for col in range(model.num_nodes):
            if row == src and col == dst:
                print(f' {phases[phase]}{switch+1:1d}', end='')
                try:
                    (switch, src, dst, phase) = next(points)
                except StopIteration:
                    pass
            else:
                print('  .', end='')
        print()
    print()


if __name__ == "__main__":
    which = 3
    if which == 0:
        num_nodes = 16
        num_switches = 4
        topo_builder = RotatingSwitches(num_switches=num_switches, interval = 4, start_offset = 1)
    elif which == 1:
        num_nodes = 11
        num_switches = 2
        topo_builder = RotatingSwitches(num_switches=num_switches, interval = 6, start_offset = 1)
    elif which == 2:
        num_nodes = 16
        num_switches = 4
        topo_builder = Rotornet2024Switches(num_switches=num_switches)
    else:
        num_nodes = 16
        num_switches = 4
        topo_builder = RotorNetLowLatency(num_switches=num_switches)
    model = Model()
    for _ in range(num_nodes):
        node = model.add_node(1500 * num_switches)
        for _ in range(num_switches):
            model.add_port(node, 450)
    
    topology = topo_builder.get_topology(model)
    model.topology = topology
    print_topology_by_switch(model)
    print()
    print_topology_by_phase(model)
    print()
    print_topology_combined(model)
