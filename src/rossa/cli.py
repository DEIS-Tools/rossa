from itertools import permutations
import sys
import os
import json
import time
from random import Random
import subprocess
import tempfile

import tomli
from . import uppaal, plotting, cppsim
from .model import Flow, Model, RotatingSwitches, Rotornet2024Switches, RotorNetLowLatency, FileTopologyBuilder
from plumbum import cli, colors, local

# import plotting


class SimpleTimings:
    def __init__(self) -> None:
        self.times = {}

    def start(self, name):
        self.times[name] = [time.monotonic_ns(), None]

    def stop(self, name):
        self.times[name][1] = time.monotonic_ns()

    def asdict(self):
        """Returns a dictionary with timings in ms"""
        return {name: (end_ns - start_ns) * 1e-6 for name, (start_ns, end_ns) in self.times.items()}


class BadConfigException(Exception):
    def __init__(self, errors, *args, **kwargs):
        self.errors = errors
        super().__init__(*args, **kwargs)


def build_flowless_model(num_nodes, bandwidth, capacity, ports_per_node, **kwargs):
    model = Model()
    for _ in range(num_nodes):
        node = model.add_node(capacity)
        for _ in range(ports_per_node):
            model.add_port(node, bandwidth)
    return model


class UniformFlowBuilder:
    def __init__(self, num_flows, min_demand, max_demand, **kwargs):
        self.num_flows = num_flows
        self.min_demand = min_demand
        self.max_demand = max_demand
        if self.num_flows is None:
            raise ValueError("Number of flows must be specified")
        if self.min_demand is None:
            raise ValueError("Min demand must be specified")
        if self.max_demand is None:
            raise ValueError("Max demand must be specified")
        if self.max_demand < self.min_demand:
            raise ValueError("Max demand cannot be less than minimum demand")

    def make_flows(self, model: Model, random: Random) -> list[Flow]:
        flows: list[Flow] = []
        for _ in range(self.num_flows):
            amount = random.randint(self.min_demand, self.max_demand)
            ingress_idx, egress_idx = random.sample(range(model.num_nodes), 2)
            ingress, egress = (model.nodes[idx] for idx in [ingress_idx, egress_idx])
            flows.append(Flow(ingress, egress, [amount]))
        return flows


class GravityFlowBuilder:
    def __init__(self, connection_percentage, min_demand, max_demand, power, generate_steps, sampling_demand_variance_percent, **kwargs):
        self.connection_percentage = connection_percentage
        self.min_demand = min_demand
        self.max_demand = max_demand
        self.power = power
        self.demand_variance = sampling_demand_variance_percent
        self.generate_steps = generate_steps
        if self.connection_percentage is None:
            raise ValueError("Percentage of all sender receiver pairs must be specified")
        if self.min_demand is None:
            raise ValueError("min_demand must be specified")
        if self.max_demand is None:
            raise ValueError("max_demand must be specified")
        if self.max_demand < self.min_demand:
            raise ValueError("max_demand cannot be less than min_demand")
        if self.power is None:
            raise ValueError("power must be specified")
        if self.demand_variance is None:
            raise ValueError("sampling_demand_variance_percent must be specified")
        else:
            self.demand_variance = float(self.demand_variance) / 100.0
        if self.generate_steps is None:
            raise ValueError("generate_steps must be specified")

    def make_flows(self, model: Model, random: Random) -> list[Flow]:
        flows: list[Flow] = []
        num_nodes = model.num_nodes
        # Assign a sender and receiver mass to each node
        mind = self.min_demand
        maxd = self.max_demand
        delta = maxd - mind
        pwr = self.power
        send_mass = [random.random() for _ in range(num_nodes)]
        recv_mass = [random.random() for _ in range(num_nodes)]

        # Take all permutations and restrict amount
        permuts = list(permutations(range(num_nodes), 2))
        num_to_use = round(len(permuts) * self.connection_percentage * 0.01)
        send_recv_pairs = random.sample(permuts, num_to_use)
        # Generate flows
        for ingress_idx, egress_idx in send_recv_pairs:
            ingress, egress = (model.nodes[idx] for idx in [ingress_idx, egress_idx])
            amount = round(mind + send_mass[ingress_idx] ** pwr * recv_mass[egress_idx] ** pwr * delta)
            amount_over_time = [int(max(1, amount * random.uniform(1 - self.demand_variance, 1 + self.demand_variance))) for _ in range(self.generate_steps)]
            flows.append(Flow(ingress, egress, amount_over_time))
        return flows

class FileFlowBuilder:
    def __init__(self, traffic_file):
        assert(os.path.isfile(traffic_file))
        self.file_path = traffic_file
    def make_flows(self, model: Model, random: Random) -> list[Flow]:
        flows: list[Flow] = []
        with open(self.file_path, "r") as file:
            for line in file:
                parts = line.split(';')
                if len(parts) != 3:
                    print(self.file_path, parts)
                assert(len(parts) == 3)
                ingress_idx, egress_idx, amounts = parts
                ingress, egress = (model.nodes[int(idx)] for idx in [ingress_idx, egress_idx])
                amount_over_time = [int(amount) for amount in amounts.split(',')]
                flows.append(Flow(ingress, egress, amount_over_time))
        return flows


_CONFIG_FLOW_BUILDERS = {"uniform": UniformFlowBuilder, "gravity": GravityFlowBuilder}

_CONFIG_TOPOLOGY_BUILDERS = {
    "rotating": RotatingSwitches,
    "rotornet2024": Rotornet2024Switches,
    "rotornetLL": RotorNetLowLatency,
}


def parse_config(config) -> tuple[UniformFlowBuilder | GravityFlowBuilder | FileFlowBuilder, RotatingSwitches | Rotornet2024Switches | RotorNetLowLatency | FileTopologyBuilder]:
    """Parses config file returns components for UPPAAL model construction or raises exception"""
    errors = []

    def enum_check(value, valid, error_message="Enum error"):
        if value not in valid:
            errors.append(error_message.format(valid_values=",".join(valid)))
    
    traffic_file = config["flow"]["traffic_file"] if "traffic_file" in config["flow"] else None
    if traffic_file is None or not os.path.isfile(traffic_file):
        enum_check(
            config["flow"]["type"],
            _CONFIG_FLOW_BUILDERS.keys(),
            "Flow type must be one of {valid_values}",
        )

    topology_file = config["topology"]["topology_file"] if "topology_file" in config["topology"] else None
    if topology_file is None or not os.path.isfile(topology_file):
        enum_check(
            config["topology"]["type"],
            _CONFIG_TOPOLOGY_BUILDERS.keys(),
            "Topology type must be one of {valid_values}",
        )

    if errors:
        raise BadConfigException(errors)

    if traffic_file is None or not os.path.isfile(traffic_file):
        flow_builder = _CONFIG_FLOW_BUILDERS[config["flow"]["type"]](**config["flow"])
    else:
        flow_builder = FileFlowBuilder(traffic_file=traffic_file)
    
    if topology_file is None or not os.path.isfile(topology_file):
        topology_builder = _CONFIG_TOPOLOGY_BUILDERS[config["topology"]["type"]](
            num_switches=config["model"]["num_switches"], **config["topology"]
        )
    else:
        topology_builder = FileTopologyBuilder(num_switches=config["model"]["num_switches"], topology_file=topology_file)

    return flow_builder, topology_builder


def str_override(s):
    if "=" not in s:
        raise ValueError(f"'{s}' is not a valid string override")
    return tuple(s.split("="))


def int_override(s):
    if "=" not in s:
        raise ValueError(f"'{s}' is not a valid integer override")
    key, sval = s.split("=")
    return (key, int(sval))


def bool_override(s):
    if "=" not in s:
        raise ValueError(f"'{s}' is not a valid integer override")
    key, sval = s.split("=")
    if sval == "true":
        return (key, True)
    elif sval == "false":
        return (key, False)
    raise ValueError(f"'{sval} in {s} is not a valid boolean")


class OutputMixin:
    verbose = False
    stdout = sys.stdout
    stderr = sys.stderr

    def output(self, text, is_error=False):
        if not is_error:
            print(text, file=self.stdout)
        else:
            print(text, file=self.stderr)

    def diagnostics(self, text):
        if self.verbose:
            self.output(text, is_error=False)


class RotorSwitchApp(cli.Application):
    PROGNAME = colors.green
    VERSION = colors.blue | "1.0.0"


def write_file_with_mkdir(content: str, file):
    out_path = local.path(file)
    out_path.dirname.mkdir()
    with open(out_path, "w") as f:
        f.write(content)

@RotorSwitchApp.subcommand("generate")
class RotorGenerate(cli.Application, OutputMixin):

    verbose = cli.Flag(["-v", "--verbose"], default=False, help="Enable verbose output")
    
    no_uppaal = cli.Flag("--fast", default = False, help="Simulate directly in C++ (skipping UPPAAL)")

    src_dir = cli.SwitchAttr(["-s", "--src_dir"], cli.ExistingDirectory)

    output_file = cli.SwitchAttr(["-o", "--output-file"], str, mandatory=True)

    export_declarations = cli.Flag(
        "--export-declarations",
        default=False,
        help="Writes global definitions to stdout",
    )

    extension_library_name = cli.SwitchAttr(["--ext-name"], str, mandatory=False, default="libcustom.so")
    # traffic_library_name = cli.SwitchAttr(["--traffic-ext-name"], str, mandatory=False, default="libtraffic_gravity_model.so")
    mode = cli.SwitchAttr(["--mode"], str, default="simulation", help="Mode. One of: simulation, verification, smc")

    config_file = cli.SwitchAttr(
        ["-c", "--config"],
        cli.ExistingFile,
        mandatory=True,
        help="A TOML configuration file containing model parameters and query options.",
    )

    string_overrides = cli.SwitchAttr(
        ["--ss"],
        str_override,
        list=True,
        help="Override string config. Eg. -ss flow.type=uniform",
    )
    integer_overrides = cli.SwitchAttr(
        ["--si"],
        int_override,
        list=True,
        help="Override config integer. Eg. -si model.num_nodes=12",
    )
    boolean_overrides = cli.SwitchAttr(
        ["--sb"],
        bool_override,
        list=True,
        help="Override config boolean. Eg. -sb query.query_sim_packets_at_node=false",
    )

    def main(self):
        self.stdout = sys.stdout
        self.stderr = sys.stderr
        self.out = self.output

        timings = SimpleTimings()

        self.diagnostics("Parsing config")
        timings.start("config_parsing")
        self.config = self._parse_config_file()
        self._apply_config_overrides()

        try:
            flow_builder, topo_builder = parse_config(self.config)
        except BadConfigException as e:
            self.output("Errors during config parsing", is_error=True)
            for error in e.errors:
                self.output(error, is_error=True)
            return

        self.diagnostics("Building Model")
        timings.start("building_model")
        random = self._get_random()
        model_config = self.config["model"]
        model = build_flowless_model(ports_per_node=model_config["num_switches"], random=random, **model_config)

        # Add topology based on switches
        model.topology = topo_builder.get_topology(model)
        if topo_builder is not FileTopologyBuilder and "topology_file" in self.config["topology"] and not os.path.isfile(self.config["topology"]["topology_file"]):
            with open(self.config["topology"]["topology_file"], "w") as file:
                json.dump(model.to_json_file_format(), file)

        # Add flows
        flows = flow_builder.make_flows(model, random=random)
        model.add_flows(flows)
        # Write flows to file, if traffic_file is specified, but it was not yet created.
        if flow_builder is not FileFlowBuilder and "traffic_file" in self.config["flow"] and not os.path.isfile(self.config["flow"]["traffic_file"]):
            with open(self.config["flow"]["traffic_file"], "w") as file:
                for flow in flows:
                    file.writelines(f'{flow.ingress.index};{flow.egress.index};' + ','.join(str(amount) for amount in flow.amount_over_time) + '\n')

        timings.stop("building_model")
        self.diagnostics("Model built")

        if self.no_uppaal:
            file_content = cppsim.write_model_declarations(model, config=self.config)
            output_file = local.path(self.output_file)
            output_file.dirname.mkdir()
            if not self.src_dir:
                self.src_dir = output_file.dirname
            sim_model_path = output_file.dirname / 'sim-model.h'
            with open(sim_model_path, "w") as f:
                f.write(file_content)
            build_dir = tempfile.mkdtemp(dir = output_file.dirname)
            scheduler_lib_path = self.src_dir / self.extension_library_name
            # traffic_lib_path = self.src_dir / self.traffic_library_name
            subprocess.run(f"cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B {build_dir} && cmake --build {build_dir} --target sim && mv {build_dir}/sim {output_file} && rm -r {build_dir}", 
                           env=dict(os.environ, scheduler_lib_path=scheduler_lib_path, sim_model_path=sim_model_path), 
                           cwd=self.src_dir, shell=True)

        elif self.export_declarations:
            self.diagnostics("Exporting definitions")
            model_definitions = uppaal.write_model_declarations(model, ext_name=self.extension_library_name)
            self.output(model_definitions)
            self.diagnostics("Definitions exported")
        else:
            self.diagnostics("Exporting UPPAAL file")
            if self.mode == "simulation":
                file_content = uppaal.write_simulation_file(model, config=self.config, ext_name=self.extension_library_name)
                write_file_with_mkdir(file_content, self.output_file)
            if self.mode == "verification":
                file_content = uppaal.write_verification_file(model, config=self.config, ext_name=self.extension_library_name, statistical=False)
                write_file_with_mkdir(file_content, self.output_file)
            if self.mode == "smc":
                file_content = uppaal.write_verification_file(model, config=self.config, ext_name=self.extension_library_name, statistical=True)
                write_file_with_mkdir(file_content, self.output_file)

    def _parse_config_file(self):
        with open(self.config_file, "rb") as f:
            config = tomli.load(f)
        return config

    def _apply_config_overrides(self):
        for skey, sval in self.string_overrides + self.integer_overrides + self.boolean_overrides:
            config = self.config
            key_path = skey.split(".")
            for part in key_path[:-1]:
                if part not in config:
                    self.output(f"Warning config override parent (sub)key '{part}' not found!")
                    config[part] = {}
                config = config[part]
            config[key_path[-1]] = sval

    def _get_random(self):
        seed = self.config.get("random", {}).get("seed", 123456)
        random = Random(seed)
        return random


@RotorSwitchApp.subcommand("run")
class RotorRun(cli.Application, OutputMixin):

    verbose = cli.Flag(["-v", "--verbose"], default=False, help="Enable verbose output")
    no_uppaal = cli.Flag("--fast", default = False, help="Simulate directly in C++ (skipping UPPAAL)")
    uppaal_key = cli.SwitchAttr(
        "--uppaal-key",
        str,
        envname="UPPAAL_KEY",
        help="The GUID license key id for UPPAAL (verifyta). Can also be set via environment variable UPPAAL_KEY",
        # mandatory=True,
    )

    model_file = cli.SwitchAttr(["-i", "--model-file"], cli.ExistingFile, mandatory=True)
    output_dir = cli.SwitchAttr(["-o", "--output-dir"], cli.ExistingDirectory)
    log_name = cli.SwitchAttr(["--logfile-name"], str, default="verifyta.log")
    segments_name = cli.SwitchAttr(["--segments-name"], str, default="segments.json")

    def main(self, env = None):
        self.verbose = True

        self.env = env

        timings = SimpleTimings()

        if not self.output_dir:
            self.output_dir = self.model_file.dirname
        path_log = self.output_dir / self.log_name
        path_segments = self.output_dir / self.segments_name
        timings_path = self.output_dir / "timings.json"

        if self.no_uppaal:
            self.diagnostics("Running simulation")
            timings.start("simulation")
            exit_code, sout, serr = self._run_cpp(self.model_file)
            timings.stop("simulation")
            self.diagnostics("Running simulation complete")
        else:
            self.diagnostics("Running UPPAAL")
            timings.start("verifyta")
            exit_code, sout, serr = self._run_verifyta(self.model_file)
            timings.stop("verifyta")
            self.diagnostics("Running UPPAAL complete")
        
        with open(timings_path, "w") as f:
            json.dump(timings.asdict(), f)

        if exit_code != 0:
            self.output(colors.warn | "There were errors", is_error=True)
            self.output(serr, is_error=True)
            return

        if self.no_uppaal:
            segments = cppsim.parse_sim_output(sout)
        else:
            segments = [s for s in uppaal.parse_uppaal_output(sout) if s.formula_expr is not None]

        with open(path_log, "w") as f:
            f.write(sout)
        # Write data file
        with open(path_segments, "w") as f:
            json_obj = []
            for segment in segments:
                json_obj.append(segment._asdict())
            json.dump(json_obj, f)
        

    def _run_cpp(self, model_path):
        result = subprocess.run(model_path, capture_output=True, shell=True, text=True, cwd=model_path.dirname, env=self.env)
        return result.returncode, result.stdout, result.stderr

    def _run_verifyta(self, model_path):
        if not self.uppaal_key:
            raise RuntimeError("UPPAAL key was not supplied")
        verifyta = local["verifyta"]
        # We omit these from console output
        secret_arguments = ["--key", self.uppaal_key]
        extra_arguments = [] # ["--upper-delta", "0.001"]
        public_args = extra_arguments + [model_path]
        self.diagnostics(f'Running verifyta with arguments: {" ".join(public_args)}')
        exit_code, sout, serr = verifyta[secret_arguments + public_args].run(retcode=None)
        return exit_code, sout, serr


@RotorSwitchApp.subcommand("plot")
class RotorPlot(cli.Application, OutputMixin):

    verbose = cli.Flag(["-v", "--verbose"], default=False, help="Enable verbose output")

    segments_file = cli.SwitchAttr(["-i", "--segments-file"], cli.ExistingFile, mandatory=True)

    output_dir = cli.SwitchAttr(["-o", "--output-dir"], str)
    plot_options = cli.SwitchAttr(["-s", "--option"], str_override, list=True)
    plot_type = cli.SwitchAttr(["-t", "--plot-type"], cli.Set("line", "boxplot"), default="line")
    transform = cli.SwitchAttr(["--transform"], cli.Set("max-y"), default=None, list=True)
    index = cli.SwitchAttr(["--index"], int, default=None)

    _OPT_VALIDATORS = {k: v[1] for k, v in plotting._DEFAULT_OPTIONS.items()}

    def _apply_transform(self, transform, data):
        if transform == "max-y":
            data = [(expr, [max(y for (_x, y) in sample) for sample in samples]) for (expr, samples) in data]
        else:
            raise Exception(f"Unknown transform '{transform}'")
        return data

    def main(self):
        out = self.output

        options = {}
        for name, str_val in self.plot_options:
            if name not in self._OPT_VALIDATORS:
                raise ValueError(f"Unknown option '{name}'")
            options[name] = self._OPT_VALIDATORS[name](str_val)

        if not self.output_dir:
            self.output_dir = self.segments_file.dirname

        with open(self.segments_file, "r") as f:
            segments = uppaal.UppaalSegment.from_file(f)

        # Write plots
        for segment in segments:
            if self.index is not None and self.index != segment.index:
                continue
            self.diagnostics(f"Segment {segment.index}")
            self.diagnostics(f"{segment.formula_expr} is {'NOT ' if not segment.is_satisfied else ''} satisfied.")
            if segment.values:
                self.diagnostics(f"Found captured values for {', '.join(segment.values.keys())}")
                data = [(expr, samples) for expr, samples in segment.values.items()]

                for transform in self.transform:
                    self.diagnostics(f"Applying transform {transform}")
                    data = self._apply_transform(transform, data)

                image_path = self.output_dir / f"plot_{segment.index:02}.png"
                self.diagnostics(f"Writing plot to {image_path}")

                if self.plot_type == "line":
                    plotting.plot_ordered_dict(data, image_path, options)
                elif self.plot_type == "boxplot":
                    plotting.plot_boxplots_ordered_dict(data, image_path, options)


if __name__ == "__main__":
    RotorSwitchApp.run()
