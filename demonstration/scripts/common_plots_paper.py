import json
import math
import sys
from dataclasses import dataclass
from typing import Sequence, Tuple, Iterable, Optional
from collections.abc import Callable
from pathlib import Path
from matplotlib import pyplot as plt
from matplotlib.axes import Axes
from matplotlib.figure import Figure
import tomli

from plumbum import FG, cli, colors, local

from rossa.uppaal import Samples, UppaalSegment

@dataclass
class InstanceData:
    name: str
    segments: Sequence[UppaalSegment]
    never_overflows: Optional[bool]

plot_settings = {
    'png': {'scale': 0.5, 'file': 'png', 'legend': True, 'fontsize': 10}, 
    'pgf': {'scale': 0.149, 'file': 'pgf', 'legend': False, 'fontsize': 8}, 
    'pdf': {'scale': 0.149, 'file': 'pdf', 'legend': False, 'fontsize': 8}
}

def get_fig_size(plot_setting):
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
line_names = {'fixed_fewest': 'Fewest hops', 
              'fixed_quickest': 'Quickest path', 
              'valiant_quickest': 'Valiant',
              'rotorlb': 'RotorLB', 
              'rotorlb_quickest': 'RotorLB*'}
line_colors = {name: plt.rcParams['axes.prop_cycle'].by_key()['color'][i] for i, name in enumerate(lines)}
line_styles = {name: {'color': line_colors[name]} for name in lines}
line_styles_bw = {name: {'linestyle': BW_LINE_STYLES[i], 'color': line_colors[name]} for i, name in enumerate(lines)}


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


def average(iterable: Iterable[float], default: float = 0.0) -> float:
    count = 0
    total = 0
    for x in iterable:
        total += x
        count += 1
    if count == 0:
        return default
    return total / count

def mylegend(fig, fontsize, anchor=(1, 0), loc="lower right", ncol=3, **kwargs):
    fig.legend(bbox_to_anchor=anchor, loc=loc, bbox_transform=fig.transFigure, ncol=ncol, fontsize=fontsize, **kwargs)

def plot_common(plot_setting, title, xlabel, ylabel):
    fig = plt.figure(figsize=get_fig_size(plot_setting))
    ax = fig.add_subplot(111)
    ax.set_title(title, fontsize=plot_setting['fontsize'])
    ax.set_xlabel(xlabel, fontsize=plot_setting['fontsize'])
    ax.set_ylabel(ylabel, fontsize=plot_setting['fontsize'])
    ax.tick_params(axis='both', labelsize=plot_setting['fontsize'])
    ax.autoscale(enable=True, axis='x', tight=True)
    ax.grid(True)
    return fig, ax

PlotLine = Tuple[str, Sequence[float], Sequence[float]]
PlotData = Sequence[PlotLine]
def plot_from_data(fig: Figure, ax: Axes, data: PlotData, output_path, plot_setting, line_styles=line_styles, **kwargs):
    for name, x_vals, y_vals in data:
        assert(name in lines)
        ax.plot(x_vals, y_vals, label=line_names[name], **line_styles[name], **kwargs)
    if plot_setting['legend']: 
        mylegend(fig, fontsize=plot_setting['fontsize'])
    fig.savefig(output_path, bbox_inches='tight')

def _port_utilization_data(aggregation: Callable[..., float], name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    data: PlotData = []
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    for name, segment in name_segments:
        samples = [_samples[0] for _samples in segment.values.values() if _samples is not None]
        ys, x_values, _ = expand_samples(samples)
        y_values = [aggregation(yvals) for yvals in zip(*ys)]
        data.append((name, x_values, y_values))
    return data
def maximum_port_utilization_data(name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    return _port_utilization_data(max, name_segments)
def average_port_utilization_data(name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    return _port_utilization_data(average, name_segments)

def _latency_data(aggregation: Callable[..., float], name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    data: PlotData = []
    for name, segment in name_segments:
        flow_values = [0] + sorted(
            # Ignore unfinished samples.
            aggregation((flow_sample[-1][1] 
                 for flow_sample in flow_samples 
                 if flow_sample is not None and flow_sample[-1][1] > 0), 
                default=500)
            for flow_samples in segment.values.values() 
            if flow_samples is not None
        )
        data.append((name, list(range(len(flow_values))), flow_values))
    return data
def maximum_latency_data(name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    return _latency_data(max, name_segments)
def average_latency_data(name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    return _latency_data(average, name_segments)

def _buffer_data(aggregation: Callable[..., float], name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    data: PlotData = []
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    for name, segment in name_segments:
        # Extract data related to overflow line.
        overflow_keys = [key for key in segment.values.keys() if key.startswith("gDidOverflow")]
        assert(len(overflow_keys) == 1)
        overflow_key = overflow_keys[0]
        # overflow_val = segment.values[overflow_key][0][0][1]
        # Get the data from the single sample for packetsAtNode(n) measurements, filtering out the overflow line.
        samples = [_samples[0] for key, _samples in segment.values.items() if key != overflow_key and _samples is not None]
        # Take maximum Y of each node
        node_values = [0] + sorted(
            aggregation((time_buffer_size[1] for time_buffer_size in node_sample)) for node_sample in samples
        )
        data.append((name, list(range(len(node_values))), node_values))
    return data
def maximum_buffer_data(name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    return _buffer_data(max, name_segments)
def average_buffer_data(name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    return _buffer_data(average, name_segments)

def _buffer_over_time_data(aggregation: Callable[..., float], name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    data: PlotData = []
    # Require only one sample per expression.
    for _name, segment in name_segments:
        ensure_one_sample_per_expression(segment)
    for name, segment in name_segments:
        # Extract data related to overflow line.
        overflow_keys = [key for key in segment.values.keys() if key.startswith("gDidOverflow")]
        assert(len(overflow_keys) == 1)
        overflow_key = overflow_keys[0]
        # overflow_val = segment.values[overflow_key][0][0][1]
        # Get the data from the single sample for packetsAtNode(n) measurements, filtering out the overflow line.
        samples = [_samples[0] for key, _samples in segment.values.items() if key != overflow_key and _samples is not None]
        ys, xvalues, _ = expand_samples(samples)
        y_values = [aggregation(yvals) for yvals in zip(*ys)]
        data.append((name, xvalues, y_values))
    return data
def maximum_buffer_over_time_data(name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    return _buffer_over_time_data(max, name_segments)
def average_buffer_over_time_data(name_segments: Sequence[Tuple[str, UppaalSegment]]) -> PlotData:
    return _buffer_over_time_data(average, name_segments)

def find_name_segment(name: str, name_segments: Sequence[Tuple[str, UppaalSegment]]):
    for n, segment in name_segments:
        if n == name:
            return segment
    return None
def find_name_plotdata(name: str, data: PlotData) -> PlotLine | None:
    for n, xs, ys in data:
        if n == name:
            return (n, xs, ys)
    return None
def restrict_to_lines(data: PlotData, lines: Sequence[str]) -> PlotData:
    result: PlotData = []
    for name in lines:
        line_data = find_name_plotdata(name, data)
        if line_data is not None:
            result.append(line_data)
    return data


def maximum_port_utilization(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plot_setting):
    fig, ax = plot_common(plot_setting, title="Maximum Port Utilization", xlabel="Phase", ylabel="Utilization")
    ax.set_ylim(ymin=0.0, auto=True)
    plot_from_data(fig, ax, maximum_port_utilization_data(name_segments), output_path, plot_setting)

def average_port_utilization(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plot_setting):
    fig, ax = plot_common(plot_setting, title="Average Port Utilization", xlabel="Phase", ylabel="Utilization")
    ax.set_ylim(ymin=0, auto=True)
    plot_from_data(fig, ax, average_port_utilization_data(name_segments), output_path, plot_setting)

def maximum_latency(name_segments: Sequence[Tuple[str, UppaalSegment]], verification_results: dict[str, Optional[bool]], output_path, plotting, plot_setting):
    fig, ax = plot_common(plot_setting, title="Maximum Latency of Flows (Ascending)", xlabel="Flow (sorted by latency)", ylabel="Time units")
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['latency_max'])
    data = [(name, xs, ys) for (name, xs, ys) in maximum_latency_data(name_segments) if verification_results[name] is None or verification_results[name]]
    plot_from_data(fig, ax, data, output_path, plot_setting)

def average_latency(name_segments: Sequence[Tuple[str, UppaalSegment]], verification_results: dict[str, Optional[bool]], output_path, plotting, plot_setting):
    fig, ax = plot_common(plot_setting, title="Average Latency of Flows (Ascending)", xlabel="Flow (sorted by latency)", ylabel="Time units")
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['latency_max'])
    data = [(name, xs, ys) for (name, xs, ys) in average_latency_data(name_segments) if verification_results[name] is None or verification_results[name]]
    plot_from_data(fig, ax, data, output_path, plot_setting, line_styles_bw)

def maximum_buffer(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting, plot_setting):
    fig, ax = plot_common(plot_setting, title="Maximum Buffer Size of Nodes (Ascending)", xlabel="Nodes (sorted by max buffer size)", ylabel="Packets")
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['packet_max'])
    plot_from_data(fig, ax, maximum_buffer_data(name_segments), output_path, plot_setting, line_styles_bw, linewidth=1, alpha=0.8)

def average_buffer(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting, plot_setting):
    fig, ax = plot_common(plot_setting, title="Average Buffer Size of Nodes (Ascending)", xlabel="Nodes (sorted by avg buffer size)", ylabel="Packets")
    ax.set_xticks(range(0, 1000, 5))
    ax.set_ylim(ymin=0.0, ymax=plotting['packet_max'])
    plot_from_data(fig, ax, average_buffer_data(name_segments), output_path, plot_setting, line_styles_bw, linewidth=1, alpha=0.8)

def maximum_buffer_over_time(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting, plot_setting):
    fig, ax = plot_common(plot_setting, title="Maximum Node Buffer Size over time", xlabel="Phase", ylabel="Packets")
    ax.set_ylim(ymin=0.0, ymax=plotting['packet_max'])
    ax.autoscale(enable=True, axis='x', tight=True)
    data = restrict_to_lines(maximum_buffer_over_time_data(name_segments), lines)
    plot_from_data(fig, ax, data, output_path, plot_setting, linewidth=1, alpha=0.8)

def average_buffer_over_time(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plotting, plot_setting):
    fig, ax = plot_common(plot_setting, title="Average Node Buffer Size over time", xlabel="Phase", ylabel="Packets")
    ax.set_ylim(ymin=0.0, ymax=plotting['packet_max'])
    data = restrict_to_lines(average_buffer_over_time_data(name_segments), lines)
    plot_from_data(fig, ax, data, output_path, plot_setting, linewidth=1, alpha=0.8)


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
def make_legend_to_file(name_segments: Sequence[Tuple[str, UppaalSegment]], output_path, plot_setting, ncol=5):
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
    lgs = legend_ax.legend(handles=plot_lines, bbox_to_anchor=(0, 0, 1, 1), bbox_transform=legend_fig.transFigure, fontsize=plot_setting['fontsize'],
                            ncol=ncol, fancybox=True, shadow=True, columnspacing=1.5, labelspacing=0.2, handlelength=2.5)
    # Remove everything else from the legend's figure
    legend_ax.axis('off')
    # Save the legend as a separate figure
    legend_fig.savefig(output_path, bbox_inches='tight', bbox_extra_artists=[lgs])


def plot(instance_datas: Sequence[InstanceData], output_folder, plotting, plot_setting):
    # Indices
    # 0: packetsAtNode + overflow
    # 1: sampleLatency [multiple samples]
    # 2: portUtilization
    file_suffix = plot_setting['file']

    maximum_port_utilization([(inst.name, inst.segments[2]) for inst in instance_datas], output_folder / f"max_port_utilization.{file_suffix}", plot_setting = plot_setting)
    average_port_utilization([(inst.name, inst.segments[2]) for inst in instance_datas], output_folder / f"avg_port_utilization.{file_suffix}", plot_setting = plot_setting)

    verification_results = {inst.name: inst.never_overflows for inst in instance_datas}
    maximum_latency([(inst.name, inst.segments[1]) for inst in instance_datas], verification_results, output_folder / f"max_latencies.{file_suffix}", plotting=plotting, plot_setting = plot_setting)
    average_latency([(inst.name, inst.segments[1]) for inst in instance_datas], verification_results, output_folder / f"avg_latencies.{file_suffix}", plotting=plotting, plot_setting = plot_setting)
    
    maximum_buffer([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"max_buffer.{file_suffix}", plotting=plotting, plot_setting = plot_setting)
    average_buffer([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"avg_buffer.{file_suffix}", plotting=plotting, plot_setting = plot_setting)

    maximum_buffer_over_time([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"max_buffer_over_time.{file_suffix}", plotting=plotting, plot_setting = plot_setting)
    average_buffer_over_time([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"avg_buffer_over_time.{file_suffix}", plotting=plotting, plot_setting = plot_setting)
    
    make_legend_to_file([(inst.name, inst.segments[0]) for inst in instance_datas], output_folder / f"legend.{file_suffix}", plot_setting = plot_setting)

def report(instance_datas: Sequence[InstanceData], output_folder):
    path = output_folder / "verification-report.txt"
    with open(path, "w") as f:
        f.write('Verification:\n')
        for inst in instance_datas:
            if inst.never_overflows is not None:
                if inst.never_overflows:
                    f.write(f'{inst.name}: Never overflows\n')
                else:
                    f.write(f'{inst.name}: Can eventually overflow\n')
        f.write('\nSMC:\n')
        for inst in instance_datas:
            if inst.segments is not None:
                f.write(f'{inst.name}:\n')
                for segment in inst.segments:
                    if segment.interval is not None and segment.confidence is not None:
                        f.write(f'{segment.formula_expr} is in [{segment.interval[0]}, {segment.interval[1]}] with confidence {segment.confidence}\n')
                    else:
                        f.write(f'{segment.formula_expr} is {segment.is_satisfied}\n')    

class CommonPlots(cli.Application):
    PROGNAME = colors.green
    VERSION = colors.blue | "0.0.1"

    output_dir = cli.SwitchAttr(["--output-dir"], str, default=".")
    quiet = cli.Flag(["--quiet"], default=False, help="Hide output from the script itself")
    pgf = cli.Flag(["--pgf"], default=False, help="Output plots as PGF instead of PNG")
    pdf = cli.Flag(["--pdf"], default=False, help="Output plots as PDF")

    def main(self):
        plot_setting = plot_settings['png']  # Default to png
        if self.pgf:
            plot_setting = plot_settings['pgf']
        elif self.pdf:
            plot_setting = plot_settings['pdf']

        self.base = local.path(self.output_dir)

        with open(self.base / "instances.toml", "rb") as f:
            data = tomli.load(f)

        plotting = data["plotting"]

        schedulers = [(c["folder_name"], self.base / c["folder_name"]) for c in data["instance"]]
        datas = []
        verification_data = []
        for s, f in schedulers: 
            segments = self._check_simulation(f)
            never_overflows = self._check_verification(f)
            if segments is not None:
                data = InstanceData(name=s, segments=segments, never_overflows=never_overflows)
                datas.append(data)
            smc_segments = self._check_smc(f)
            verification_data.append(InstanceData(name=s, segments=smc_segments, never_overflows=never_overflows))
        if len(datas) > 0:
            plot(datas, output_folder=self.base, plotting=plotting, plot_setting=plot_setting)
        if len(verification_data) > 0:
            report(verification_data, output_folder=self.base)

    def _read_segments(self, path) -> Sequence[UppaalSegment]:
        with open(path, "r") as f:
            segments = json.load(f)
            return [UppaalSegment(s["is_satisfied"], s["formula_expr"], s["values"], s["index"], s["interval"], s["confidence"]) for s in segments]

    def _check_simulation(self, folder) -> Optional[Sequence[UppaalSegment]]:
        segments_path = folder / "segments.json"
        if segments_path.is_file():
            try:
                return self._read_segments(segments_path)
            except:
                self._out(f"Error reading from {segments_path}", is_error=True)
        return None
            
    def _check_verification(self, folder) -> Optional[bool]:
        verification_path = Path(folder / "segments-verification.json")
        if verification_path.is_file():
            segments = self._read_segments(verification_path)
            assert(len(segments) == 1)
            return segments[0].is_satisfied
        return None
    def _check_smc(self, folder) -> Optional[Sequence[UppaalSegment]]:
        smc_path = Path(folder / "segments-smc.json")
        if smc_path.is_file():
            return self._read_segments(smc_path)
        return None

    def _out(self, text, is_error=False):
        if not self.quiet:
            stream = sys.stderr if is_error else sys.stdout
            print(text, file=stream)


if __name__ == "__main__":
    CommonPlots.run()
