import os
import re
import csv
import io
from collections import defaultdict
import collections.abc
from typing import Dict, Literal, Sequence, Tuple, Union, NamedTuple
from .model import Model
from .uppaal import data_file_contents, UppaalSegment

__all__ = ['write_model_declarations', 'parse_sim_output']

ModelType = Union[Literal["base"], Literal["sampling"]]

DECLARATION_TEMPLATE = {
    "base": "sim-model.h",
    "sampling": "sim-model.h"
}

def write_array(data, new_line_depth=1, indentation='', indent_with="  ", line_suffix=''):
    if isinstance(data, collections.abc.Sequence) and not isinstance(data, str):
        if new_line_depth > 0:
            sub_results = [write_array(elem, new_line_depth-1, indentation=indentation+indent_with, indent_with=indent_with, line_suffix=line_suffix) for elem in data]
            return indentation + '{' + f'{line_suffix}\n' + f',{line_suffix}\n'.join(sub_results) + f'{line_suffix}\n' + indentation + '}'
        else:
            sub_results = [write_array(elem, new_line_depth-1, indentation='', indent_with=indent_with, line_suffix=line_suffix) for elem in data]
            return indentation + '{' + ', '.join(sub_results) + '}'
    return indentation + str(data)

def write_array_linestart(data, new_line_depth=1, indentation='', indent_with="  ", line_suffix=''):
    new_line_depth -= 1
    if new_line_depth > 0:
        sub_results = [write_array(elem, new_line_depth, indentation=indentation+indent_with, indent_with=indent_with, line_suffix=line_suffix) for elem in data]
        return indentation + '{' + f'{line_suffix}\n' + f',{line_suffix}\n'.join(sub_results) + f'{line_suffix}\n' + indentation + '}'
    else:
        sub_results = [write_array(elem, new_line_depth, indentation='', indent_with=indent_with, line_suffix=line_suffix) for elem in data]
        indent = indentation + indent_with
        return indentation + '{' + f'{line_suffix}\n' + indent + ', '.join(sub_results) + f'{line_suffix}\n' + indentation + '}'



def apply_substitutions(model: Model, template_declarations: str, model_type: ModelType, config: dict):
    query_config = config.get("query", dict())
    sim_steps = query_config.get('sim_steps', 50)
    sampling_steps = query_config.get('sampling_steps', 200)
    sampling_count = query_config.get('sampling_count', 50)

    # port_owners = write_array_linestart(p.owner.index for p in model.ports)
    gen_node_capacities = write_array_linestart((n.capacity for n in model.nodes), line_suffix='\\')
    gen_port_bandwidths = write_array_linestart((p.bandwidth for p in model.ports), line_suffix='\\')
    gen_topology = write_array(model.topology, line_suffix='\\')
    gen_flows = write_array([(f.ingress.index, f.egress.index, f.amount) for f in model.flows], line_suffix='\\')

    # Add random sampling variances.
    flow_config = config.get('flow', dict())
    sampling_demand_variance_percent = flow_config.get('sampling_demand_variance_percent', 0)
    demand_random = sampling_demand_variance_percent / 100.0
    demand_injection = "const packet_t amount = FLOWS[f].amount;"
    if model_type == "sampling" and sampling_demand_variance_percent > 0:
        demand_injection = f"""const double fAmount = fmax(0.0, FLOWS[f].amount * (1.0 + random({demand_random * 2}) - {demand_random})); const packet_t amount = (int)trunc(round(fAmount));""".strip()

    schedule_config = config.get('schedule', dict())
    # Avoid strings masquarading as false true values.
    gen_schedule_toggle = 'reschedule(gCurrentPhase);' if schedule_config.get('reschedule', False) == True else '//rescheduling disabled in generation: //reschedule(gCurrentPhase);'

    substitutions = {
        'NUM_NODES': model.num_nodes,
        'NUM_FLOWS': model.num_flows,
        'NUM_PHASES': model.num_phases,
        # 'NUM_PORTS': model.num_ports,
        'NUM_SWITCHES': model.num_switches,
        # 'GEN_PORT_OWNER': port_owners,
        'GEN_NODE_CAPACITIES': gen_node_capacities,
        'GEN_PORT_BANDWIDTHS': gen_port_bandwidths,
        'GEN_TOPOLOGY': gen_topology,
        'GEN_FLOWS': gen_flows,
        'GEN_SCHEDULE_TOGGLE': gen_schedule_toggle,
        'DEMAND_INJECTION': demand_injection,
        # 'EXT_NAME': ext_name,
        'SIM_STEPS': sim_steps,
        'SAMPLING_STEPS': sampling_steps,
        'SAMPLING_COUNT': sampling_count
    }

    def replace_fn(matchobj):
        return str(substitutions[matchobj.group(1)])

    return re.sub(r'<<([^>]+)>>', replace_fn, template_declarations)

def write_model_declarations(
        model: Model,
        model_type: ModelType,
        config: dict) -> str:
    data_file = DECLARATION_TEMPLATE[model_type]
    template_declarations = data_file_contents(data_file)
    return apply_substitutions(model, template_declarations, model_type, config)


Points2D = Sequence[Tuple[float, float]]
Samples = Sequence[Points2D]

class RossaLine(NamedTuple):
    step: int
    did_overflow: bool
    packets_at_node: Dict[str, float]
    port_utilization: Dict[str, float]
    
    @classmethod
    def from_csv_row(cls, row) -> "RossaLine":
        packets_at_node: Dict[str, float] = {}
        port_utilization: Dict[str, float] = {}
        for key in row:
            if key.startswith('packetsAtNode'):
                packets_at_node[key] = float(row[key])
            elif key.startswith('portUtilization'):
                port_utilization[key] = float(row[key])
        return RossaLine(step = int(row['step']), 
                         did_overflow = row['gDidOverflow'].lower() == 'true', 
                         packets_at_node = packets_at_node, 
                         port_utilization = port_utilization)
    @classmethod
    def from_csv_str(cls, csv_string: str) -> list["RossaLine"]:
        csv_file = io.StringIO(csv_string)
        reader = csv.DictReader(csv_file, delimiter=';', skipinitialspace=True)
        return [RossaLine.from_csv_row(row) for row in reader]

class RossaSampleLine(NamedTuple):
    sample_id: int
    # step: int
    latency: Dict[str, float]
    @classmethod
    def from_csv_row(cls, row) -> "RossaSampleLine":
        latency: Dict[str, float] = {}
        for key in row:
            if key.startswith('sampleLatency'):
                latency[key] = float(row[key])
        return RossaSampleLine(sample_id = int(row['sample_id']), latency = latency)
    @classmethod
    def from_csv_str(cls, csv_string: str) -> list["RossaSampleLine"]:
        csv_file = io.StringIO(csv_string)
        reader = csv.DictReader(csv_file, delimiter=';', skipinitialspace=True)
        return [RossaSampleLine.from_csv_row(row) for row in reader]

def to_samples(s: Sequence[Tuple[int, float]]) -> Samples:
    return [[(float(a), b) for (a,b) in s]]

def to_latency_samples(s: Sequence[float], sampling_steps: int) -> Samples:
    return [[(0, latency), (sampling_steps, latency if latency >= 0 else sampling_steps)] for latency in s]

class RossaData(NamedTuple):
    did_overflow: Sequence[Tuple[int, bool]]
    packets_at_nodes: Dict[str, Sequence[Tuple[int, float]]]
    port_utilizations: Dict[str, Sequence[Tuple[int, float]]]
    flow_samples: Dict[str, Sequence[float]]

    @classmethod
    def from_rossa_lines(cls, lines: list[RossaLine], sample_lines: list[RossaSampleLine]) -> "RossaData":
        packets_at_nodes = defaultdict(list)
        port_utilizations = defaultdict(list)
        for line in lines:
            for key, value in line.packets_at_node.items():
                packets_at_nodes[key].append((line.step, value))
            for key, value in line.port_utilization.items():
                port_utilizations[key].append((line.step, value))
        flow_samples = defaultdict(list)
        for line in sample_lines:
            for key, value in line.latency.items():
                flow_samples[key].append(value)
        return RossaData(did_overflow = sorted(((line.step, line.did_overflow) for line in lines), key = lambda x: x[0]),
                         packets_at_nodes = {key: sorted(values, key = lambda x: x[0]) for key, values in packets_at_nodes.items()},
                         port_utilizations = {key: sorted(values, key = lambda x: x[0]) for key, values in port_utilizations.items()},
                         flow_samples = flow_samples)
    @classmethod
    def from_csv_str(cls, csv_string: str) -> "RossaData":
        splits = csv_string.split('@@@\n', maxsplit=1)
        port_and_node_string = splits[0]
        samples_string = splits[1]
        return cls.from_rossa_lines(RossaLine.from_csv_str(port_and_node_string), RossaSampleLine.from_csv_str(samples_string))
    
    def overflow_to_samples(self, on_val: float, off_val: float = 0.0) -> Samples:
        values: Sequence[Tuple[int, float]] = []
        start_step = self.did_overflow[0][0]
        state = self.did_overflow[0][1]
        values.append((start_step, on_val if state else off_val))
        end_step = self.did_overflow[-1][0]
        for step, new_state in self.did_overflow:
            if new_state != state:
                values.append((step, on_val if state else off_val))
                state = new_state
                values.append((step, on_val if state else off_val))
            elif step == end_step:
                values.append((step, on_val if state else off_val))
        return to_samples(values)

    def to_uppaal_segments(self) -> list[UppaalSegment]:
        ## These UPPAAL segments are hardcoded to match what the plotter expects. This corresponds to the query configs used in uppaal.write_file()
        num_nodes = len(self.packets_at_nodes)
        num_ports = len(self.port_utilizations)
        num_flows = len(self.flow_samples)
        max_capacity = 1500 * (num_ports // num_nodes)
        sim_steps = 500
        sampling_steps = 300
        sampling_count = len(next(iter(self.flow_samples.values())))

        overflow_samples = {f'gDidOverflow * {max_capacity}': self.overflow_to_samples(max_capacity)}
        
        packets_at_nodes = dict(overflow_samples) 
        packets_at_nodes.update({key: to_samples(value) for key, value in self.packets_at_nodes.items()})
        queryValues = [f'packetsAtNode({i})' for i in range(num_nodes)]
        sim_packets_at_node = f'simulate [#<={sim_steps}] {{gDidOverflow * {max_capacity}, {", ".join(queryValues)}}}'
        query_sim_packets_at_node = UppaalSegment(is_satisfied = True, formula_expr = sim_packets_at_node, values = packets_at_nodes, index = 0)

        # Note: The new model does not have per-port buffers,
        packets_at_ports = dict(overflow_samples)
        queryValues = [f'totalPortBuffered({i})' for i in range(num_ports)]
        packets_at_ports.update({key: None for key in queryValues})
        sim_packets_at_port = f'simulate [#<={sim_steps}] {{gDidOverflow * {max_capacity}, {", ".join(queryValues)}}}'
        query_sim_packets_at_port = UppaalSegment(is_satisfied = True, formula_expr = sim_packets_at_port, values = packets_at_ports, index = 1)

        latencies = [f'sampleLatency[{i}]' for i in range(num_flows)]
        sim_sampled_latencies = f'simulate [#<={sampling_steps};{sampling_count}] {{{", ".join(latencies)}}}'
        query_sim_sampled_latency = UppaalSegment(is_satisfied = True, formula_expr=sim_sampled_latencies, values = {key: to_latency_samples(value, sampling_steps) for key, value in self.flow_samples.items()}, index = 2)
        
        utilizations = [f'portUtilization({p})' for p in range(num_ports)]
        sim_port_utilization = f'simulate [#<={sim_steps}] {{{", ".join(utilizations)}}}'
        query_sim_port_utilization = UppaalSegment(is_satisfied = True, formula_expr=sim_port_utilization, values = {key: to_samples(value) for key, value in self.port_utilizations.items()}, index = 3)

        return [query_sim_packets_at_node, query_sim_packets_at_port, query_sim_sampled_latency, query_sim_port_utilization]


def parse_sim_output(data: str) -> list[UppaalSegment]:
    return RossaData.from_csv_str(data).to_uppaal_segments()
