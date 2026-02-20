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

plot_settings = {'png': {'scale': 0.5, 'file': 'png', 'legend': True}, 'pgf': {'scale': 0.16, 'file': 'pgf', 'legend': False}}
plot_setting = plot_settings['png']  # Default to png

def get_fig_size():
    _FIG_SCALING = plot_setting['scale']
    _FIG_WIDTH = 24 * _FIG_SCALING
    _FIG_HEIGTH = 14 * _FIG_SCALING
    return (_FIG_WIDTH, _FIG_HEIGTH)

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

lines = ['fixed_fewest', 'fixed_quickest', 'valiant_quickest', 'rotorlb_quickest', 'rotorlb']
# line_names = {name: name for name in lines}
line_names = {'fixed_fewest': 'Fewest hops', 
              'fixed_quickest': 'Quickest path', 
              'valiant_quickest': 'Valiant',
              'rotorlb': 'RotorLB', 
              'rotorlb_quickest': 'RotorLB*'}
# line_names = {'fixed_fewest': 'fixed\\_fewest', 
#               'fixed_quickest': 'fixed\\_quickest', 
#               'valiant_quickest': 'valiant\\_quickest',
#               'rotorlb': 'rotorlb', 
#               'rotorlb_quickest': 'rotorlb\\_quickest', 
#               'overflow': 'overflow'}
line_colors = {name: plt.rcParams['axes.prop_cycle'].by_key()['color'][i] for i, name in enumerate(lines)}
line_styles = {name: {'linestyle': BW_LINE_STYLES[i], 'color': line_colors[name]} for i, name in enumerate(lines)}


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


def average(iterable, default=0):
    count = 0
    total = 0
    for x in iterable:
        total += x
        count += 1
    if count == 0:
        return default
    return total / count

def mylegend(fig, anchor=(1, 0), loc="lower right", ncol=3, fontsize=10, **kwargs):
    fig.legend(bbox_to_anchor=anchor, loc=loc, bbox_transform=fig.transFigure, ncol=ncol, fontsize=fontsize, **kwargs)

def average_port_utilization(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path):
    fig = plt.figure(figsize=get_fig_size())
    ax = fig.add_subplot(111)
    ax.set_title("Average Port Utilization", fontsize=10)
    ax.set_xlabel("Phase", fontsize=10)
    ax.set_ylabel("Utilization", fontsize=10)
    ax.set_ylim(ymin=0, auto=True)
    ax.grid(True)
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    for name, segment in name_segments:
        assert(name in lines)
        samples = [_samples[0] for _samples in segment.values.values()]
        ys, xvalues, _ = expand_samples(samples)
        num_samples = len(samples)
        avgs = [sum(yvals) / num_samples for yvals in zip(*ys)]
        ax.plot(xvalues, avgs, label=line_names[name], color=line_colors[name])
    if plot_setting['legend']: 
        mylegend(fig)
    fig.savefig(output_path)

def maximum_port_utilization(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path):
    fig = plt.figure(figsize=get_fig_size())
    ax = fig.add_subplot(111)
    ax.set_title("Maximum Port Utilization", fontsize=10)
    ax.set_xlabel("Phase", fontsize=10)
    ax.set_ylabel("Utilization", fontsize=10)
    ax.set_ylim(ymin=0.0, auto=True)
    ax.grid(True)
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    for name, segment in name_segments:
        assert(name in lines)
        samples = [_samples[0] for _samples in segment.values.values()]
        ys, xvalues, _ = expand_samples(samples)
        avgs = [max(yvals) for yvals in zip(*ys)]
        ax.plot(xvalues, avgs, label=line_names[name], color=line_colors[name])
    if plot_setting['legend']:
        mylegend(fig)
    fig.savefig(output_path)

def maximum_latency(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting):
    fig = plt.figure(figsize=get_fig_size())
    ax = fig.add_subplot(111)
    ax.set_title("Maximum Latency of Flows (Ascending)", fontsize=10)
    ax.set_xlabel("Flow (sorted by latency)", fontsize=10)
    ax.set_ylabel("Time units", fontsize=10)
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['latency_max'])
    ax.grid(True)
    for name, segment in name_segments:
        assert(name in lines)
        if (name not in lines[2:]): continue
        # Each Expr is a particular flow
        # Multiple sample for each.
        # Sampling latency we just need the last heighest number.
        # Take maximum Y of each sample
        flow_maximums = sorted(
            # Ignore unfinished samples.
            max((flow_sample[-1][1] for flow_sample in flow_samples if flow_sample is not None and flow_sample[-1][1] > 0), default=500) for flow_samples in segment.values.values()
        )
        ax.plot(list(range(len(flow_maximums))), flow_maximums, label=line_names[name], color=line_colors[name])
    if plot_setting['legend']:
        mylegend(fig, anchor=(0, 1), loc="upper left")
    fig.savefig(output_path)

def average_latency(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting):
    fig = plt.figure(figsize=get_fig_size())
    ax = fig.add_subplot(111)
    ax.set_title("Average Latency of Flows (Ascending)", fontsize=10)
    ax.set_xlabel("Flow (sorted by latency)", fontsize=10)
    ax.set_ylabel("Time units", fontsize=10)
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['latency_max'])
    ax.grid(True)
    for name, segment in name_segments:
        assert(name in lines)
        # Each Expr is a particular flow
        # Multiple sample for each.
        # Sampling latency we just need the last heighest number.
        # Then take average
        # Take maximum Y of each sample
        flow_averages = sorted(
            average((flow_sample[-1][1] for flow_sample in flow_samples if flow_sample is not None and flow_sample[-1][1] > 0), default=500) for flow_samples in segment.values.values()
        )
        ax.plot(list(range(len(flow_averages))), flow_averages, label=line_names[name], **line_styles[name])
    if plot_setting['legend']:
        mylegend(fig, anchor=(0, 1), loc="upper left")
    fig.savefig(output_path)

def average_buffer(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting):
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    fig = plt.figure(figsize=get_fig_size())
    ax = fig.add_subplot(111)
    ax.set_title("Maximum Buffer Size of Nodes (Ascending)", fontsize=10)
    ax.set_xlabel("Nodes (sorted by max buffer size)", fontsize=10)
    ax.set_ylabel("Packets", fontsize=10)
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['packet_max']) # auto=True)
    ax.grid(True)
    for name, segment in name_segments:
        assert(name in lines)
        # Extract data related to overflow line.
        overflow_keys = [key for key in segment.values.keys() if key.startswith("gDidOverflow")]
        assert(len(overflow_keys) == 1)
        overflow_key = overflow_keys[0]
        overflow_val = segment.values[overflow_key][0][0][1]
        # Get the data from the single sample for packetsAtNode(n) measurements, filtering out the overflow line.
        samples = [_samples[0] for key, _samples in segment.values.items() if key != overflow_key]
        # Take maximum Y of each node
        node_maximums = sorted(
            average((time_buffer_size[1] for time_buffer_size in node_sample)) for node_sample in samples
        )
        ax.plot(list(range(len(node_maximums))), node_maximums, label=line_names[name], linewidth=1, alpha=0.8, **line_styles[name])
    # ax.axhline(y=6000, color = line_colors['overflow'], linestyle=line_styles['overflow']['linestyle'])
    if plot_setting['legend']:
        mylegend(fig, anchor=(0, 1), loc="upper left")
    fig.savefig(output_path)

def maximum_buffer(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting):
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    fig = plt.figure(figsize=get_fig_size())
    ax = fig.add_subplot(111)
    ax.set_title("Maximum Buffer Size of Nodes (Ascending)", fontsize=10)
    ax.set_xlabel("Nodes (sorted by max buffer size)", fontsize=10)
    ax.set_ylabel("Packets", fontsize=10)
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['packet_max']) # auto=True)
    ax.grid(True)
    for name, segment in name_segments:
        assert(name in lines)
        # Extract data related to overflow line.
        overflow_keys = [key for key in segment.values.keys() if key.startswith("gDidOverflow")]
        assert(len(overflow_keys) == 1)
        overflow_key = overflow_keys[0]
        overflow_val = segment.values[overflow_key][0][0][1]
        # Get the data from the single sample for packetsAtNode(n) measurements, filtering out the overflow line.
        samples = [_samples[0] for key, _samples in segment.values.items() if key != overflow_key]
        # Take maximum Y of each node
        node_maximums = sorted(
            max((time_buffer_size[1] for time_buffer_size in node_sample)) for node_sample in samples
        )
        ax.plot(list(range(len(node_maximums))), node_maximums, label=line_names[name], linewidth=1, alpha=0.8, **line_styles[name])
    # ax.axhline(y=6000, color = line_colors['overflow'], linestyle=line_styles['overflow']['linestyle'])
    if plot_setting['legend']:
        mylegend(fig, anchor=(0, 1), loc="upper left")
    fig.savefig(output_path)

def find_name_segment(name: str, name_segments: Sequence[Tuple[str, UppaalSegment]]):
    for n, segment in name_segments:
        if n == name:
            return segment
    return None

def maximum_buffer_over_time(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting):
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    fig = plt.figure(figsize=get_fig_size())
    ax = fig.add_subplot(111)
    ax.set_title("Maximum Node Buffer Size over time", fontsize=10)
    ax.set_xlabel("Phase", fontsize=10)
    ax.set_ylabel("Packets", fontsize=10)
    # ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['packet_max']) # auto=True)
    ax.grid(True)
    for name in lines:
      segment = find_name_segment(name, name_segments)
      if not segment is None:
    # for name, segment in name_segments:
        assert(name in lines)
        # Extract data related to overflow line.
        overflow_keys = [key for key in segment.values.keys() if key.startswith("gDidOverflow")]
        assert(len(overflow_keys) == 1)
        overflow_key = overflow_keys[0]
        overflow_val = segment.values[overflow_key][0][0][1]
        # Get the data from the single sample for packetsAtNode(n) measurements, filtering out the overflow line.
        samples = [_samples[0] for key, _samples in segment.values.items() if key != overflow_key]
        ys, xvalues, _ = expand_samples(samples)
        avgs = [max(yvals) for yvals in zip(*ys)]
        ax.plot(xvalues, avgs, label=line_names[name], color=line_colors[name], linewidth=1, alpha=0.8) #  , linestyle=line_styles[name]['linestyle'])

        # Take maximum Y of each node
        # node_maximums = sorted(
        #     max((time_buffer_size[1] for time_buffer_size in node_sample)) for node_sample in samples
        # )
        # ax.plot(list(range(len(node_maximums))), node_maximums, label=name, linestyle=line_styles[name], color=line_colors[name])
    # ax.axhline(y=6000, color = line_colors['overflow'], linestyle=line_styles['overflow']['linestyle'])
    if plot_setting['legend']:
        mylegend(fig, anchor=None, loc="upper right")
    fig.savefig(output_path)

def average_buffer_over_time(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting):
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    fig = plt.figure(figsize=get_fig_size())
    ax = fig.add_subplot(111)
    ax.set_title("Maximum Node Buffer Size over time", fontsize=10)
    ax.set_xlabel("Phase", fontsize=10)
    ax.set_ylabel("Packets", fontsize=10)
    # ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['packet_max']) # auto=True)
    ax.grid(True)
    for name, segment in name_segments:
        assert(name in lines)
        # Extract data related to overflow line.
        overflow_keys = [key for key in segment.values.keys() if key.startswith("gDidOverflow")]
        assert(len(overflow_keys) == 1)
        overflow_key = overflow_keys[0]
        overflow_val = segment.values[overflow_key][0][0][1]
        # Get the data from the single sample for packetsAtNode(n) measurements, filtering out the overflow line.
        samples = [_samples[0] for key, _samples in segment.values.items() if key != overflow_key]
        ys, xvalues, _ = expand_samples(samples)
        num_nodes = len(samples)
        avgs = [sum(yvals)/num_nodes for yvals in zip(*ys)]
        ax.plot(xvalues, avgs, label=line_names[name], color=line_colors[name], linewidth=1, alpha=0.8) #  , linestyle=line_styles[name])

        # Take maximum Y of each node
        # node_maximums = sorted(
        #     max((time_buffer_size[1] for time_buffer_size in node_sample)) for node_sample in samples
        # )
        # ax.plot(list(range(len(node_maximums))), node_maximums, label=name, linestyle=line_styles[name], color=line_colors[name])
    # ax.axhline(y=6000, color = line_colors['overflow'], linestyle=line_styles['overflow']['linestyle'])
    if plot_setting['legend']:
        mylegend(fig, anchor=(0, 1), loc="upper left")
    fig.savefig(output_path)

def make_legend(lines, loc="lower right", ncol=1, lgd_alpha = 0.8):
    ax = plt.gca()
    # lgtext = [d[0] for d in data]
    lg = ax.legend(handles=lines, ncol=ncol, loc=loc, fancybox=True, 
                   shadow=True if lgd_alpha == 1.0 else False)
    fr = lg.get_frame()
    fr.set_lw(1)
    fr.set_alpha(lgd_alpha)
    fr.set_edgecolor('black')
    return lg
def make_legend_to_file(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, ncol=5):
    plot_lines = []
    for name in ['fixed_fewest', 'fixed_quickest', 'valiant_quickest', 'rotorlb', 'rotorlb_quickest']:
        segment = find_name_segment(name, name_segments)
        if not segment is None:
            label = line_names[name] if name in line_names else name
            line, = plt.plot([0.0], color = line_styles[name]['color'], label=label)
            plot_lines.append(line)
    # ---------------------------------------------------------------------
    # Create a separate figure for the legend
    # ---------------------------------------------------------------------
    # Bounding box for legend (in order to get proportions right)
    # Issuing a draw command seems necessary in order to ensure the layout
    # happens correctly; I was getting weird bounding boxes without it.
    lg = make_legend(plot_lines,lgd_alpha=1)
    fig, ax = plt.subplots()
    fig.canvas.draw()
    # This gives pixel coordinates for the legend bbox (although perhaps 
    # if you are using a different renderer you might get something else).
    legend_bbox = lg.get_tightbbox(fig.canvas.get_renderer())
    # Convert pixel coordinates to inches
    legend_bbox = legend_bbox.transformed(fig.dpi_scale_trans.inverted())

    # Create a separate figure for the legend, with appropriate width and height
    legend_fig, legend_ax = plt.subplots(figsize=(legend_bbox.width, legend_bbox.height))

    # Recreate the legend on the separate figure/axis
    lgs = legend_ax.legend(handles=plot_lines, bbox_to_anchor=(0, 0, 1, 1), bbox_transform=legend_fig.transFigure, fontsize=10,
                            ncol=ncol, fancybox=True, shadow=True, columnspacing=1.5, labelspacing=0.2, handlelength=2.5)
    # Remove everything else from the legend's figure
    legend_ax.axis('off')
    # Save the legend as a separate figure
    legend_fig.savefig(output_path, bbox_inches='tight', bbox_extra_artists=[lgs])

    # fig = plt.figure(figsize=get_fig_size())
    # mylegend(fig, anchor=(0, 1), loc="upper left", labels=labels[])
    # fig.savefig(output_path)


def plot(instance_datas: Sequence[InstanceData], output_folder, plotting):
    # Indices
    # 0: packetsAtNode + overflow
    # 1: totalPortBuffered + overflow
    # 2: sampleLatency [multiple samples]
    # 3: portUtilization
    file_suffix = plot_setting['file']

    average_port_utilization(
        [(inst.name, inst.segments[3]) for inst in instance_datas], output_folder / f"avg_port_utilization.{file_suffix}"
    )
    maximum_port_utilization(
        [(inst.name, inst.segments[3]) for inst in instance_datas], output_folder / f"max_port_utilization.{file_suffix}"
    )
    # Latency for rescheduled instances messes with the sampling, so they are excluded
    maximum_latency([(inst.name, inst.segments[2]) for inst in instance_datas if not inst.reschedule], output_folder / f"max_latencies.{file_suffix}", plotting=plotting)
    average_latency([(inst.name, inst.segments[2]) for inst in instance_datas if not inst.reschedule], output_folder / f"avg_latencies.{file_suffix}", plotting=plotting)
    
    maximum_buffer([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"max_buffer.{file_suffix}", plotting=plotting)
    average_buffer([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"avg_buffer.{file_suffix}", plotting=plotting)

    maximum_buffer_over_time([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"max_buffer_over_time.{file_suffix}", plotting=plotting)
    average_buffer_over_time([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"avg_buffer_over_time.{file_suffix}", plotting=plotting)
    
    make_legend_to_file([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"legend.{file_suffix}")


class CommonPlots(cli.Application):
    PROGNAME = colors.green
    VERSION = colors.blue | "0.0.1"

    output_dir = cli.SwitchAttr(["--output-dir"], str, default=".")
    quiet = cli.Flag(["--quiet"], default=False, help="Hide output from the script itself")
    pgf = cli.Flag(["--pgf"], default=False, help="Output plots as PGF instead of PNG")
    if pgf:
        plot_setting = plot_settings['pgf']

    def main(self):
        self.base = local.path(self.output_dir)

        with open(self.base / "instances.toml", "rb") as f:
            data = tomli.load(f)

        plotting = data["plotting"]

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
        plot(datas, output_folder=self.base, plotting=plotting)

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
