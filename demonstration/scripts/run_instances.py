import multiprocessing
import os
import re
import shlex
import stat
from os import chmod, environ
from queue import Empty
from time import sleep
from typing import NamedTuple

import plumbum.cli
import tomli
from plumbum import local
import plumbum

from rnetwork import cli, uppaal

class Instance(NamedTuple):
    extension_name: str
    folder_name: str
    modes: list[str]
    environment_vars: dict = {}

plot_settings = {
    'png': {'scale': 0.5, 'file': 'png', 'legend': True, 'fontsize': 10}, 
    'pgf': {'scale': 0.145, 'file': 'pgf', 'legend': False, 'fontsize': 8}, 
    'pdf': {'scale': 0.145, 'file': 'pdf', 'legend': False, 'fontsize': 8}
}

def get_fig_size(plot_setting):
    _FIG_SCALING = plot_setting['scale']
    _FIG_WIDTH = 24 * _FIG_SCALING
    _FIG_HEIGTH = 14 * _FIG_SCALING
    return (_FIG_WIDTH, _FIG_HEIGTH)

line_names = {'fixed_fewest': 'Fewest hops', 
              'fixed_quickest': 'Quickest path', 
              'valiant_quickest': 'Valiant',
              'rotorlb': 'RotorLB', 
              'rotorlb_quickest': 'RotorLB*'}

def label_packets_buffered(text: str):
    text = text.replace("totalPortBuffered", "Buf")
    text = re.sub("gDidOverflow \\* \\d+", "Overflow", text)
    text = re.sub("packetsAtNode", "Node", text)
    return text


def label_packets_latency(text: str):
    text = text.replace("sampleLatency", "Sample Flow")
    return text


def make_single_plots(folder, ex: Instance, segments_path, plot_cfg, plot_setting):
    packet_max = plot_cfg.get("packet_max", 2000)
    latency_max = plot_cfg.get("latency_max", 40)

    with open(segments_path, "r") as f:
        segments = uppaal.UppaalSegment.from_file(f)
    
    file_suffix = plot_setting['file']

    seg_node_packets = next((s for s in segments if s.index == 0), None)
    plot_segment_line(
        folder / f"node_buffered.{file_suffix}",
        seg_node_packets,
        title=f"Packets buffered ({line_names[ex.folder_name]})",
        xlabel="Time",
        ylabel="Packets",
        ymax=packet_max,
        label_fn=label_packets_buffered,
        plot_setting=plot_setting
    )

    seg_latency_packets = next((s for s in segments if s.index == 1), None)
    plot_segment_line(
        folder / f"sampling_latencies.{file_suffix}",
        seg_latency_packets,
        title=f"Sampled latency ({line_names[ex.folder_name]})",
        xlabel="Time",
        ylabel="Latency",
        ymax=latency_max,
        label_fn=label_packets_latency,
        plot_setting=plot_setting
    )

    plot_latency_boxplot(folder / f"sampling_latencies_boxplot.{file_suffix}", seg_latency_packets, "Sampled Latency", xlabel="Flow", ylabel="Latency", ymax=latency_max, plot_setting=plot_setting)

def run_for_instance(ex: Instance, folder, plotting_cfg, plot_setting, force=False, no_uppaal=False):
    # Ensure folder
    instance_folder = folder / ex.folder_name
    if not instance_folder.is_dir():
        instance_folder.mkdir()
    
    model_cfg_file = folder / 'model_configuration.toml'
    
    if no_uppaal:
        # Generate model
        model_path = instance_folder / f"sim"
        if force or (not model_path.is_file()):
            print(f"Generating model {model_path} for {ex}")
            cli.RotorGenerate.invoke(
                config_file=model_cfg_file,
                output_file=model_path,
                extension_library_name=ex.extension_name,
                no_uppaal=True,
                src_dir=local.cwd,
            )
        log_name = instance_folder / "cpp-simulation.log"
        segments_name = instance_folder / "segments.json"
        # Run simulation 
        if force or (not log_name.is_file() or log_name.stat().st_mtime < model_path.stat().st_mtime):
            with local.env(**ex.environment_vars):
                cli.RotorRun.invoke({k: str(v) for k,v in ex.environment_vars.items()} if ex.environment_vars else None,
                    model_file=model_path, log_name=log_name, segments_name=segments_name, no_uppaal=no_uppaal, output_dir = instance_folder
                )
        # Plotting
        print(f"Generating plots for {instance_folder}")
        make_single_plots(instance_folder, ex, segments_name, plotting_cfg, plot_setting=plot_setting)
    else:            
        MODE_NAME_SUFFIX = {'simulation': '', 'verification': '-verification', 'smc': '-smc'}
        # Generate model
        for mode in ex.modes:
            if mode not in MODE_NAME_SUFFIX.keys(): continue
            model_path = instance_folder / f"model{MODE_NAME_SUFFIX[mode]}.xml"
            if force or (not model_path.is_file()):
                print(f"Generating {mode} model {model_path} for {ex}")
                cli.RotorGenerate.invoke(
                    config_file=model_cfg_file,
                    output_file=model_path,
                    extension_library_name=ex.extension_name,
                    src_dir=local.cwd,
                    mode=mode
                )
                # Generate script with proper environment variables to open model
                str_env_vars = " ".join(f"{k}={shlex.quote(str(v))}" for k, v in ex.environment_vars.items())
                uppaal_script = instance_folder / f"uppaal{MODE_NAME_SUFFIX[mode]}.sh"
                with open(uppaal_script, "w") as f:
                    f.write(f"{str_env_vars} uppaal {shlex.quote(model_path)}\n")
                script_stats = os.stat(uppaal_script)
                chmod(uppaal_script, script_stats.st_mode | stat.S_IEXEC)
            
            log_name = instance_folder / f"verifyta{MODE_NAME_SUFFIX[mode]}.log"
            segments_name = instance_folder / f"segments{MODE_NAME_SUFFIX[mode]}.json"
            # Run UPPAAL
            if force or (not log_name.is_file() or log_name.stat().st_mtime < model_path.stat().st_mtime):
                with local.env(**ex.environment_vars):
                    cli.RotorRun.invoke(
                        model_file=model_path, log_name=log_name, segments_name=segments_name, uppaal_key=environ["UPPAAL_KEY"]
                    )
            # Plotting
            if mode == "simulation":
                print(f"Generating plots for {instance_folder}")
                make_single_plots(instance_folder, ex, segments_name, plotting_cfg, plot_setting=plot_setting)


def mp_worker(queue, worker_name, folder, kwargs):
    print(f"Worker {worker_name} started")
    instances_config = read_config(folder / 'instances.toml')
    plotting_cfg = instances_config.get("plotting", {})

    while True:
        try:
            instance = queue.get_nowait()
            if instance is None:
                break
            print(f"Worker {worker_name} starting {instance.folder_name}")
            run_for_instance(instance, folder, plotting_cfg, **kwargs)
            queue.task_done()
        except Empty:
            print(f"Worker {worker_name} done")
            break


def mp_main(base_folder, num_workers=4, **kwargs):
    instances_config = read_config(base_folder / 'instances.toml')
    instances = read_instances(instances_config)

    queue = multiprocessing.JoinableQueue()
    procs = []
    for i in range(num_workers):
        name = f"mp_worker{i}"
        p = multiprocessing.Process(target=mp_worker, args=(queue, name, base_folder, kwargs))
        p.daemon = True
        procs.append(p)

    for instance in instances:
        queue.put(instance)

    for p in procs:
        p.start()

    for p in procs:
        p.join()


def read_instances(instance_config):
    return [
        Instance(
            extension_name=instance["extension_name"],
            folder_name=instance["folder_name"],
            modes=instance["modes"],
            environment_vars=instance.get("envs", {}),
        )
        for instance in instance_config["instance"]
    ]


def read_config(path):
    with open(path, "rb") as f:
        return tomli.load(f)


def shorten_label(text, start=5, end=7, middle=".."):
    if len(text) <= (start + end + len(middle)):
        return text
    return f"{text[:start]}{middle}{text[-end:]}"


def plot_segment_line(path, segment: uppaal.UppaalSegment, title, xlabel, ylabel, plot_setting, ymax=None, label_fn=None):
    import matplotlib.pyplot as plt
    import matplotlib.colors as colors
    import random

    data = [(expr, samples) for expr, samples in segment.values.items() if not label_fn is None and label_fn(expr) != 'Overflow']

    fig = plt.figure(figsize=get_fig_size(plot_setting))
    ax = fig.add_subplot(111)
    ax.set_title(title, fontsize=plot_setting['fontsize'])
    ax.set_xlabel(xlabel, fontsize=plot_setting['fontsize'])
    ax.set_ylabel(ylabel, fontsize=plot_setting['fontsize'])
    ax.tick_params(axis='both', labelsize=plot_setting['fontsize'])
    if ymax:
        ax.set_ylim(ymin=0, ymax=ymax)
    else:
        ax.set_ylim(ymin=0, auto=True)
    ax.autoscale(enable=True, axis='x', tight=True)
    ax.grid(True)
    color_list = list(colors.XKCD_COLORS)
    random.seed(35)
    random.shuffle(color_list)
    for (expr, samples), color in zip(data, color_list):
        if samples is None: continue
        for xy in samples:
            x = [d[0] for d in xy]
            y = [d[1] for d in xy]
            ax.plot(x, y, label=label_fn(expr) if label_fn else shorten_label(expr), linewidth=1, alpha=0.8, color=color)

    # fig.legend(fontsize=plot_setting['fontsize'])
    fig.savefig(path, bbox_inches='tight')
    plt.close(fig)


def plot_latency_boxplot(path, segment: uppaal.UppaalSegment, title, xlabel, ylabel, plot_setting, ymax=None, label_fn=None):
    import matplotlib.pyplot as plt

    data = [(expr, samples) for expr, samples in segment.values.items()]

    fig = plt.figure(figsize=get_fig_size(plot_setting))
    ax = fig.add_subplot(111)
    ax.set_title(title, fontsize=plot_setting['fontsize'])
    ax.set_xlabel(xlabel, fontsize=plot_setting['fontsize'])
    ax.set_ylabel(ylabel, fontsize=plot_setting['fontsize'])
    ax.tick_params(axis='both', labelsize=plot_setting['fontsize'])
    if ymax:
        ax.set_ylim(ymin=0, ymax=ymax)
    else:
        ax.set_ylim(ymin=0, auto=True)
    ax.grid(True)

    bp = []
    for i, (expr, samples) in enumerate(data):
        latencies = [flow_sample[-1][1] for flow_sample in samples if flow_sample[-1][1] > 0]
        bp.append(latencies)
    ax.boxplot(bp, showmeans=True, meanline=True)
    fig.savefig(path, bbox_inches='tight')
    plt.close(fig)


def main(folder, **kwargs):
    instance_config = read_config(folder / 'instances.toml')
    instances = read_instances(instance_config)
    plotting_cfg = instance_config.get("plotting", {})
    for instance in instances:
        print(f"Running for {instance.folder_name}")
        run_for_instance(instance, folder, plotting_cfg, **kwargs)

class CaseStudyCli(plumbum.cli.Application):
    
    no_uppaal = plumbum.cli.Flag("--fast", default = False, help="Simulate directly in C++ (skipping UPPAAL)")
    uppaal_key = plumbum.cli.SwitchAttr(
        "--uppaal-key",
        str,
        envname="UPPAAL_KEY",
        help="The GUID license key id for UPPAAL (verifyta). Can also be set via environment variable UPPAAL_KEY",
        # mandatory=True,
    )
    # TODO: Flag attribute for force
    force = plumbum.cli.Flag(["--force"], help="Force regeneration of artefacts")
    
    pgf = plumbum.cli.Flag(["--pgf"], default=False, help="Output plots as PGF instead of PNG")
    pdf = plumbum.cli.Flag(["--pdf"], default=False, help="Output plots as PDF")

    directory = plumbum.cli.SwitchAttr(["-d", "--directory"], plumbum.cli.ExistingDirectory, default=local.path('.'))
    num_workers = plumbum.cli.SwitchAttr(['-j', '--num-workers'], int, default=1)

    def main(self):    
        plot_setting = plot_settings['png']
        if self.pgf:
            plot_setting = plot_settings['pgf']
        elif self.pdf:
            plot_setting = plot_settings['pdf']
        base_folder = local.path(self.directory)
        print(f"Generating for folder {base_folder}")
        if self.num_workers > 1:
            mp_main(base_folder, self.num_workers, plot_setting=plot_setting, force=self.force, no_uppaal=self.no_uppaal)
        else:
            main(base_folder, plot_setting=plot_setting, force=self.force, no_uppaal=self.no_uppaal)


if __name__ == "__main__":
    import time
    start = time.time()
    CaseStudyCli.run()
    duration = time.time() - start
    print(f"Overall running time {duration} seconds")
