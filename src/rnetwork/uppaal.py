import collections.abc
import json
import os
import re
from typing import Dict, Literal, Optional, Sequence, Tuple, Union, NamedTuple

from .model import Model

__all__ = ['write_file', 'write_model_declarations', 'parse_uppaal_output']

ModelType = Union[Literal["base"], Literal["sampling"]]

DECLARATION_TEMPLATE = {
    "base": "model_declarations.c",
    "sampling": "model_declarations_sampling.c"
}

MODEL_TEMPLATE = {
    "base": "model_template.xml",
    "sampling": "model_template_sampling.xml"
}

XML_CHARS_REPLACE = {
    '\'': '&apos;',
    '"': '&quot;',
    '<': '&lt;',
    '>': '&gt;',
    '&': '&amp;'
}
RE_XLM_CHARS = re.compile(f'[${"".join(XML_CHARS_REPLACE.keys())}]')


def apply_substitutions(model: Model, template_declarations: str, model_type: ModelType, config: dict, ext_name: str):
    port_owners = write_array_linestart(p.owner.index for p in model.ports)
    gen_port_capacities = write_array_linestart(p.capacity for p in model.ports)
    gen_port_bandwidths = write_array_linestart(p.bandwidth for p in model.ports)
    gen_topology = write_array(model.topology)
    gen_flows = write_array([(f.ingress.index, f.egress.index, f.amount) for f in model.flows])

    # Add random sampling variances.
    flow_config = config.get('flow', dict())
    sampling_demand_variance_percent = flow_config.get('sampling_demand_variance_percent', 0)
    demand_random = sampling_demand_variance_percent / 100.0
    demand_injection = "    const packet_t amount = FLOWS[f].amount;"
    if model_type == "sampling" and sampling_demand_variance_percent > 0:
        demand_injection = f"""
    const double fAmount = fmax(0.0, FLOWS[f].amount * (1.0 + random({demand_random * 2}) - {demand_random}));
    const packet_t amount = fint(round(fAmount));
""".strip()
        
    schedule_config = config.get('schedule', dict())
    # Avoid strings masquarading as false true values.
    gen_schedule_toggle = 'reschedule(gCurrentPhase);' if schedule_config.get('reschedule', False) == True else '//rescheduling disabled in generation\n// reschedule(gCurrentPhase);'

    substitutions = {
        'NUM_NODES': model.num_nodes,
        'NUM_FLOWS': model.num_flows,
        'NUM_PHASES': model.num_phases,
        'NUM_PORTS': model.num_ports,
        'GEN_PORT_OWNER': port_owners,
        'GEN_PORT_CAPACITIES': gen_port_capacities,
        'GEN_PORT_BANDWIDTHS': gen_port_bandwidths,
        'GEN_TOPOLOGY': gen_topology,
        'GEN_FLOWS': gen_flows,
        'GEN_SCHEDULE_TOGGLE': gen_schedule_toggle,
        'DEMAND_INJECTION': demand_injection,
        'EXT_NAME': ext_name
    }

    def replace_fn(matchobj):
        return str(substitutions[matchobj.group(1)])

    return re.sub(r'<<([^>]+)>>', replace_fn, template_declarations)


def write_model_declarations(
        model: Model,
        template: ModelType,
        config: dict,
        ext_name) -> str:
    data_file = DECLARATION_TEMPLATE[template]
    template_declarations = data_file_contents(data_file)
    return apply_substitutions(model, template_declarations, template, config, ext_name)


def escape_xml(text: str) -> str:
    return RE_XLM_CHARS.sub(lambda m: XML_CHARS_REPLACE[m.group(0)], text)


def write_file(model: Model, config: dict, model_type: ModelType, ext_name='libcustom.so'):
    query_config = config.get("query", dict())
    sim_steps = query_config.get('sim_steps', 50)
    sampling_steps = query_config.get('sampling_steps', 200)
    sampling_count = query_config.get('sampling_count', 50)

    max_capacity = max(p.capacity for p in model.ports)

    uppaal_template = data_file_contents(MODEL_TEMPLATE[model_type])
    declarations = write_model_declarations(model, model_type, config, ext_name)

    queryValues = [f'packetsAtNode({i})' for i in range(model.num_nodes)]
    sim_packets_at_node = f'simulate [#<={sim_steps}] {{gDidOverflow * {max_capacity}, {", ".join(queryValues)}}}'
    queryValues = [f'totalPortBuffered({i})' for i in range(model.num_ports)]
    sim_packets_at_port = f'simulate [#<={sim_steps}] {{gDidOverflow * {max_capacity}, {", ".join(queryValues)}}}'
    max_sent = f'simulate [#<={sim_steps}] {{maxSendFromPortInPhase, extGetPacketsInNetwork()}}'

    latencies = [f'sampleLatency[{i}]' for i in range(model.num_flows)]
    sim_sampled_latencies = f'simulate [#<={sampling_steps};{sampling_count}] {{{", ".join(latencies)}}}'
    utilizations = [f'portUtilization({p})' for p in range(model.num_ports)]
    sim_port_utilization = f'simulate [#<={sim_steps}] {{{", ".join(utilizations)}}}'

    formula_candidates = [
        ('query_sim_packets_at_node', 'Packets at Node', sim_packets_at_node),
        ('query_sim_packets_at_port', 'Packets at Port', sim_packets_at_port),
        ('query_sim_max_sent', 'Max packets sent from a port in a phase', max_sent),
        ('query_verify_check_overflow', 'Overflow Check', 'A[] gDidOverflow == 0'),
        ('query_sim_sampled_latency', 'Sampled Latencies', sim_sampled_latencies),
        ('query_sim_port_utilization', 'Port utilization', sim_port_utilization)
    ]

    formulas = [
        (comment, query) for config_name, comment, query in formula_candidates
        if query_config.get(config_name, False)
    ]

    xml_queries = []
    for comment, formula in formulas:
        padding = ' ' * 8
        lines = [
            f'<formula>{escape_xml(formula)}</formula>',
            f'<comment>{escape_xml(comment)}</comment>' if comment else '<comment/>'
        ]
        xml_queries.append('\n'.join(f'{padding}{line}' for line in lines))

    gen_queries = '\n'.join([f'  <query>\n{x_query}\n  </query>' for x_query in xml_queries])

    substitutions = {
        'DECLARATIONS': escape_xml(declarations),
        'GEN_QUERIES': gen_queries,
    }

    def replace_fn(matchobj):
        return str(substitutions[matchobj.group(1)])

    result = re.sub(r'<<([^>]+)>>', replace_fn, uppaal_template)
    # The importer does not like blank leading text.
    return result.strip()


def data_file(relative_path, mode='r'):
    path = os.path.join(os.path.dirname(__file__), 'data', relative_path)
    return open(path, mode=mode)


def data_file_contents(relative_path, mode='r'):
    with data_file(relative_path, mode=mode) as f:
        data = f.read()
    return data


def indent(obj, level=1, indentation='  '):
    prefix = indentation * level
    if isinstance(obj, str):
        return '\n'.join(prefix + s for s in obj.splitlines())
    return [prefix + str(s) for s in obj]


def write_array(data, new_line_depth=1, indentation='', indent_with="  "):
    if isinstance(data, collections.abc.Sequence) and not isinstance(data, str):
        if new_line_depth > 0:
            sub_results = [write_array(elem, new_line_depth-1, indentation=indentation+indent_with) for elem in data]
            return indentation + '{\n' + ',\n'.join(sub_results) + '\n' + indentation + '}'
        else:
            sub_results = [write_array(elem, new_line_depth-1, indentation='') for elem in data]
            return indentation + '{' + ', '.join(sub_results) + '}'
    return indentation + str(data)


def write_array_linestart(data, new_line_depth=1, indentation='', indent_with="  "):
    new_line_depth -= 1
    if new_line_depth > 0:
        sub_results = [write_array(elem, new_line_depth, indentation=indentation+indent_with) for elem in data]
        return indentation + '{\n' + ',\n'.join(sub_results) + '\n' + indentation + '}'
    else:
        sub_results = [write_array(elem, new_line_depth, indentation='') for elem in data]
        indent = indentation + indent_with
        return indentation + '{\n' + indent + ', '.join(sub_results) + '\n' + indentation + '}'


def inline_array(elems):
    return write_array(elems, new_line_depth=0)


RE_SEGMENT_FORMULA_EXPR = re.compile(r'^ -- Formula: (.*)$', flags=re.MULTILINE)
# Captures all simulated traces of subexpressions.
RE_SEGMENT_EXPR_VALUES = re.compile(r'^(\w.+):$\n((^\[\d+\]: .*$\n)+)', flags=re.MULTILINE)
RE_SEGMENT_VALUES = re.compile(r'^(\[\d+\]): (.*)$', flags=re.MULTILINE)


Expr = str
Points2D = Sequence[Tuple[float, float]]
Samples = Sequence[Points2D]


class UppaalSegment(NamedTuple):
    is_satisfied: Optional[bool]
    formula_expr: Optional[str]
    values: Dict[Expr, Samples]
    index: int

    @classmethod
    def from_file(cls, f):
        jsegments = json.load(f)
        return [
            UppaalSegment(
                formula_expr=s["formula_expr"],
                is_satisfied=s["is_satisfied"],
                values=s["values"],
                index=s["index"],
            )
            for s in jsegments
        ]


def parse_uppaal_segment(data: str, index: int) -> UppaalSegment:
    """An UPPAAL segment starts with verifying"""

    # Determine formula status if any.
    if 'Formula is satisfied.' in data:
        is_satisfied = True
    elif 'Formula is NOT satisfied.' in data:
        is_satisfied = False
    else:
        is_satisfied = None

    # Extract expression
    formula_expr = None
    if match := RE_SEGMENT_FORMULA_EXPR.search(data):
        formula_expr = match.group(1)

    # Match values
    values = collections.OrderedDict()
    for match in RE_SEGMENT_EXPR_VALUES.finditer(data):
        expr = match.group(1)
        values_text = match.group(2)
        traces = []
        for m2 in RE_SEGMENT_VALUES.finditer(values_text):
            # _identifier = m2.group(1)
            pairs = [x.strip(')(') for x in m2.group(2).split(' ')]
            pairs_nums = [tuple(float(x) for x in p.split(',')) for p in pairs]
            traces.append(pairs_nums)
        values[expr] = traces
    return UppaalSegment(is_satisfied, formula_expr, values, index)


def parse_uppaal_output(data: str) -> list[UppaalSegment]:
    # Get down to some managable pieces.
    segments = []
    segment = []
    for line in data.splitlines():
        if line.startswith('Verifying formula'):
            segments.append('\n'.join(segment))
            segment = []
        else:
            segment.append(line)
    if segment:
        segments.append('\n'.join(segment))
    # Throw away first segment which is before the queries.
    segments = segments[1:]
    segments = [parse_uppaal_segment(segment, index) for index, segment in enumerate(segments)]
    return segments
