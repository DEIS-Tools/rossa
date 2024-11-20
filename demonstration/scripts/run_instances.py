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
    reschedule: bool = False
    environment_vars: dict = {}


def label_packets_buffered(text: str):
    text = text.replace("totalPortBuffered", "Buf")
    text = re.sub("gDidOverflow \\* \d+", "Overflow", text)
    return text


def label_packets_latency(text: str):
    text = text.replace("sampleLatency", "Sample Flow")
    return text


def make_single_plots(folder, ex: Instance, segments_path, plot_cfg):
    packet_max = plot_cfg.get("packet_max", 2000)
    latency_max = plot_cfg.get("latency_max", 40)

    with open(segments_path, "r") as f:
        segments = uppaal.UppaalSegment.from_file(f)

    seg_port_packets = next((s for s in segments if s.index == 1), None)

    plot_segment_line(
        folder / "port_buffered.png",
        seg_port_packets,
        title=f"Packets buffered ({ex.folder_name})",
        xlabel="Time",
        ylabel="Packets",
        ymax=packet_max,
        label_fn=label_packets_buffered,
    )

    seg_latency_packets = next((s for s in segments if s.index == 2), None)
    plot_segment_line(
        folder / "sampling_latencies.png",
        seg_latency_packets,
        title=f"Sampled latency ({ex.folder_name})",
        xlabel="Time",
        ylabel="Latency",
        ymax=latency_max,
        label_fn=label_packets_latency,
    )

    plot_latency_boxplot(folder / "sampling_latencies_boxplot.png", seg_latency_packets, "Sampled Latency", xlabel="Flow", ylabel="Latency", ymax=latency_max)


def run_for_instance(ex: Instance, folder, plotting_cfg, force=False):
    # Ensure folder
    instance_folder = folder / ex.folder_name
    if not instance_folder.is_dir():
        instance_folder.mkdir()

    # Generate model
    model_path = instance_folder / "model.xml"
    model_cfg_file = folder / 'model_configuration.toml'
    if force or (not model_path.is_file()):
        print(f"Generating model {model_path} for {ex}")
        cli.RotorGenerate.invoke(
            config_file=model_cfg_file,
            model_type="sampling",
            output_file=model_path,
            extension_library_name=ex.extension_name,
            boolean_overrides=[(f"schedule.reschedule", ex.reschedule)],
        )
        # Generate script with proper environment variables to open model
        str_env_vars = " ".join(f"{k}={shlex.quote(str(v))}" for k, v in ex.environment_vars.items())
        uppaal_script = instance_folder / "uppaal.sh"
        with open(uppaal_script, "w") as f:
            f.write(f"{str_env_vars} uppaal {shlex.quote(model_path)}\n")
        script_stats = os.stat(uppaal_script)
        chmod(uppaal_script, script_stats.st_mode | stat.S_IEXEC)

    # # Run UPPAAL
    log_name = instance_folder / "verifyta.log"
    segments_name = instance_folder / "segments.json"
    if force or (not log_name.is_file() or log_name.stat().st_mtime < model_path.stat().st_mtime):
        with local.env(**ex.environment_vars):
            cli.RotorRun.invoke(
                model_file=model_path, log_name=log_name, segments_name=segments_name, uppaal_key=environ["UPPAAL_KEY"]
            )

    print(f"Generating plots for {model_path}")
    make_single_plots(instance_folder, ex, segments_name, plotting_cfg)

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
            reschedule=instance["reschedule"],
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


def plot_segment_line(path, segment: uppaal.UppaalSegment, title, xlabel, ylabel, ymax=None, label_fn=None):
    import matplotlib.pyplot as plt

    data = [(expr, samples) for expr, samples in segment.values.items()]

    fig = plt.figure(figsize=(12, 7))
    ax = fig.add_subplot(111)
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if ymax:
        ax.set_ylim(ymin=0, ymax=ymax)
    else:
        ax.set_ylim(ymin=0, auto=True)
    ax.grid(True)

    for expr, samples in data:
        for xy in samples:
            x = [d[0] for d in xy]
            y = [d[1] for d in xy]
            ax.plot(x, y, label=label_fn(expr) if label_fn else shorten_label(expr))

    fig.legend()
    fig.savefig(path)
    plt.close(fig)


def plot_latency_boxplot(path, segment: uppaal.UppaalSegment, title, xlabel, ylabel, ymax=None, label_fn=None):
    import matplotlib.pyplot as plt

    data = [(expr, samples) for expr, samples in segment.values.items()]

    fig = plt.figure(figsize=(12, 7))
    ax = fig.add_subplot(111)
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
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
    fig.savefig(path)
    plt.close(fig)


def main(folder, force=False):
    instance_config = read_config(folder / 'instances.toml')
    instances = read_instances(instance_config)
    plotting_cfg = instance_config.get("plotting", {})
    for instance in instances:
        print(f"Running for {instance.folder_name}")
        run_for_instance(instance, folder, plotting_cfg, force=force)

class CaseStudyCli(plumbum.cli.Application):
    
    uppaal_key = plumbum.cli.SwitchAttr(
        "--uppaal-key",
        str,
        envname="UPPAAL_KEY",
        help="The GUID license key id for UPPAAL (verifyta). Can also be set via environment variable UPPAAL_KEY",
        mandatory=True,
    )
    # TODO: Flag attribute for force
    force = plumbum.cli.Flag(["--force"], help="Force regeneration of artefacts")

    directory = plumbum.cli.SwitchAttr(["-d", "--directory"], plumbum.cli.ExistingDirectory, default=local.path('.'))
    num_workers = plumbum.cli.SwitchAttr(['-j', '--num-workers'], int, default=1)

    def main(self):
        base_folder = local.path(self.directory)
        print(f"Generating for folder {base_folder}")
        if self.num_workers > 1:
            mp_main(base_folder, self.num_workers, force=self.force)
        else:
            main(base_folder, force=self.force)


if __name__ == "__main__":
    import time
    start = time.time()
    CaseStudyCli.run()
    duration = time.time() - start
    print(f"Overall running time {duration} seconds")
