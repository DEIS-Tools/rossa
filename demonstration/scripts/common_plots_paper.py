import json
import math
import sys
from dataclasses import dataclass
from typing import Sequence, Tuple
from matplotlib import pyplot as plt
import tomli

from plumbum import FG, cli, colors, local

from rnetwork.uppaal import Samples, UppaalSegment


@dataclass
class InstanceData:
    name: str
    segments: Sequence[UppaalSegment]
    reschedule: bool = False


_FIG_SCALING = 0.5
_FIG_WIDTH = 24 * _FIG_SCALING
_FIG_HEIGTH = 14 * _FIG_SCALING

# Lines styles that are distinguishable under black/white only.
BW_LINE_STYLES = [
    'solid',
    'dotted',
    'dashed',
    'dashdot',
    (5, (10, 3)), # long dash offset
    (0, (3, 5, 1, 5)), # dashdotted
    (0, (3, 1, 1, 1, 1, 1)), # densely dash-dotted
    (0, (5, 1)), # densely dashed
]


def get_latest(curve, xpositions):
    """Extract the latest seen value for all xpositions."""
    # Assert x increasing
    assert all(a[0] < b[0] for a, b in zip(curve[:-1], curve[1:]))
    i = 0
    n = len(curve)
    ys = []
    for x in xpositions:
        # At the end use last
        if i + 1 >= n:
            ys.append(curve[i][1])
            continue
        # Keep skipping until next is greater
        while (i + 1) < n and curve[i + 1][0] <= x:
            i += 1
        ys.append(curve[i][1])
    return ys


def get_bounds(samples):
    """Get the minimum/maximum x and ys across several samples/curves"""
    min_x = min(min(xy[0] for xy in sample) for sample in samples)
    max_x = max(max(xy[0] for xy in sample) for sample in samples)
    min_y = min(min(xy[1] for xy in sample) for sample in samples)
    max_y = max(max(xy[1] for xy in sample) for sample in samples)
    return min_x, max_x, min_y, max_y


def expand_samples(samples: Samples):
    """Expand to create all xy points in the set of samples"""
    # Find maximum X
    max_x = max(max(xy[0] for xy in sample) for sample in samples)
    min_x = min(min(xy[0] for xy in sample) for sample in samples)
    xvalues = list(range(math.floor(min_x), math.ceil(max_x + 0.5)))
    ys = [get_latest(sample, xvalues) for sample in samples]
    return ys, xvalues, (min_x, max_x)


def ensure_one_sample_per_expression(segment: UppaalSegment):
    for _expr, values in segment.values.items():
        assert len(values) == 1


def average(iterable):
    count = 0
    total = 0
    for x in iterable:
        total += x
        count += 1
    return total / count


def average_port_utilization(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path):
    fig = plt.figure(figsize=(_FIG_WIDTH, _FIG_HEIGTH))
    ax = fig.add_subplot(111)
    ax.set_title("Average Port Utilization")
    ax.set_xlabel("Phase")
    ax.set_ylabel("Utilization")
    ax.set_ylim(ymin=0, auto=True)
    ax.grid(True)
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    for name, segment in name_segments:
        samples = [_samples[0] for _samples in segment.values.values()]
        ys, xvalues, _ = expand_samples(samples)
        num_samples = len(samples)
        avgs = [sum(yvals) / num_samples for yvals in zip(*ys)]
        ax.plot(xvalues, avgs, label=name)
    mylegend(fig)
    fig.savefig(output_path)


def maximum_port_utilization(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path):
    fig = plt.figure(figsize=(_FIG_WIDTH, _FIG_HEIGTH))
    ax = fig.add_subplot(111)
    ax.set_title("Maximum Port Utilization")
    ax.set_xlabel("Phase")
    ax.set_ylabel("Utilization")
    ax.set_ylim(ymin=0.0, auto=True)
    ax.grid(True)
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    for name, segment in name_segments:
        samples = [_samples[0] for _samples in segment.values.values()]
        ys, xvalues, _ = expand_samples(samples)
        avgs = [max(yvals) for yvals in zip(*ys)]
        ax.plot(xvalues, avgs, label=name)
    mylegend(fig)
    fig.savefig(output_path)


def maximum_latency(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path):
    fig = plt.figure(figsize=(_FIG_WIDTH, _FIG_HEIGTH))
    ax = fig.add_subplot(111)
    ax.set_title("Maximum Latency of Flows (Ascending)")
    ax.set_xlabel("Flow (sorted by latency)")
    ax.set_ylabel("Time units")
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, auto=True)
    ax.grid(True)
    for (name, segment), linestyle in zip(name_segments, BW_LINE_STYLES):
        # Each Expr is a particular flow
        # Multiple sample for each.
        # Sampling latency we just need the last heighest number.
        # Take maximum Y of each sample
        flow_maximums = sorted(
            # Ignore unfinished samples.
            max(flow_sample[-1][1] for flow_sample in flow_samples if flow_sample[-1][1] > 0) for flow_samples in segment.values.values()
        )
        ax.plot(list(range(len(flow_maximums))), flow_maximums, label=name, linestyle=linestyle)
    mylegend(fig, anchor=(0, 1), loc="upper left")
    fig.savefig(output_path)


def mylegend(fig, anchor=(1, 0), loc="lower right", ncol=3):
    fig.legend(bbox_to_anchor=anchor, loc=loc, bbox_transform=fig.transFigure, ncol=ncol)


def average_latency(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path):
    fig = plt.figure(figsize=(_FIG_WIDTH, _FIG_HEIGTH))
    ax = fig.add_subplot(111)
    ax.set_title("Average Latency of Flows (Ascending)")
    ax.set_xlabel("Flow (sorted by latency)")
    ax.set_ylabel("Time units")
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, auto=True)
    ax.grid(True)
    for (name, segment), linestyle in zip(name_segments, BW_LINE_STYLES):
        # Each Expr is a particular flow
        # Multiple sample for each.
        # Sampling latency we just need the last heighest number.
        # Then take average
        # Take maximum Y of each sample
        flow_averages = sorted(
            average(flow_sample[-1][1] for flow_sample in flow_samples if flow_sample[-1][1] > 0) for flow_samples in segment.values.values()
        )
        ax.plot(list(range(len(flow_averages))), flow_averages, label=name, linestyle=linestyle)

    mylegend(fig, anchor=(0, 1), loc="upper left")
    fig.savefig(output_path)


def plot(instance_datas: Sequence[InstanceData], output_folder):
    # Indices
    # 0: packetsAtNode + overflow
    # 1: totalPortBuffered + overflow
    # 2: sampleLatency [multiple samples]
    # 3: portUtilization

    average_port_utilization(
        [(inst.name, inst.segments[3]) for inst in instance_datas], output_folder / "avg_port_utilization.png"
    )
    maximum_port_utilization(
        [(inst.name, inst.segments[3]) for inst in instance_datas], output_folder / "max_port_utilization.png"
    )
    # Latency for rescheduled instances messes with the sampling, so they are excluded
    maximum_latency([(inst.name, inst.segments[2]) for inst in instance_datas if not inst.reschedule], output_folder / "max_latencies.png")
    average_latency([(inst.name, inst.segments[2]) for inst in instance_datas if not inst.reschedule], output_folder / "avg_latencies.png")


class CommonPlots(cli.Application):
    PROGNAME = colors.green
    VERSION = colors.blue | "0.0.1"

    output_dir = cli.SwitchAttr(["--output-dir"], str, default=".")
    quiet = cli.Flag(["--quiet"], default=False, help="Hide output from the script itself")

    def main(self):
        self.base = local.path(self.output_dir)

        with open(self.base / "instances.toml", "rb") as f:
            data = tomli.load(f)

        schedulers = [(c["folder_name"], self.base / c["folder_name"], c["reschedule"]) for c in data["instance"]]
        datas = []
        for s, f, is_reschedule in schedulers:
            segments_path = f / "segments.json"
            self._out(segments_path)
            try:
                segments = self._read_segments(segments_path)
            except:
                self._out(f"Error reading from {segments_path}", is_error=True)
                continue
            data = InstanceData(name=s, segments=segments, reschedule=is_reschedule)
            datas.append(data)
        plot(datas, output_folder=self.base)

    def _read_segments(self, path):
        with open(path, "r") as f:
            segments = json.load(f)
            return [UppaalSegment(s["is_satisfied"], s["formula_expr"], s["values"], s["index"]) for s in segments]

    def _out(self, text, is_error=False):
        if not self.quiet:
            stream = sys.stderr if is_error else sys.stdout
            print(text, file=stream)


if __name__ == "__main__":
    CommonPlots.run()
