from typing import Dict
from collections import ChainMap
import matplotlib.pyplot as plt

_DEFAULT_OPTIONS = {
    "width": (8, int),
    "height": (5, int),
    "xmin": (None, int),
    "xmax": (None, int),
    "ymin": (None, int),
    "ymax": (None, int),
    "title": (None, str),
    "xlabel": (None, str),
    "ylabel": (None, str),
}


_DEFAULT_OPTIONS_VALS = {k: v[0] for k, v in _DEFAULT_OPTIONS.items()}


def shorten_label(text, start=5, end=7, middle=".."):
    if len(text) <= (start + end + len(middle)):
        return text
    return f"{text[:start]}{middle}{text[-end:]}"


def wrap_options(supplied: Dict = None):
    if supplied is None:
        supplied = {}
    return ChainMap(supplied, _DEFAULT_OPTIONS_VALS)


def new_figure(options: Dict = None):
    o = wrap_options(options)

    fig = plt.figure(figsize=(o["width"], o["height"]))
    ax = fig.add_subplot(111)

    if any(o[x] is not None for x in ["xmin", "xmax"]):
        ax.set_xlim([o["xmin"], o["xmax"]])
    if any(o[x] is not None for x in ["ymin", "ymax"]):
        ax.set_ylim([o["ymin"], o["ymax"]])
    if title := o["title"]:
        ax.set_title(title)
    if xlabel := o["xlabel"]:
        ax.set_xlabel(xlabel)
    if ylabel := o["ylabel"]:
        ax.set_ylabel(ylabel)

    ax.grid(True)

    return fig, ax


def plot_ordered_dict(data, path, options: Dict = None):
    fig, ax = new_figure(options)
    for expr, samples in data:
        for xy in samples:
            x = [d[0] for d in xy]
            y = [d[1] for d in xy]
            ax.plot(x, y, label=shorten_label(expr))
    fig.legend()
    fig.savefig(path)
    plt.close(fig)


def plot_boxplots_ordered_dict(data, path, options: Dict = None):
    fig, ax = new_figure(options)
    exprs = [d[0] for d in data]
    ys = [d[1] for d in data]
    ax.boxplot(x=ys)
    ax.set_xticklabels(exprs)
    fig.savefig(path)
    plt.close(fig)


def demo():
    pass


if __name__ == "__main__":
    demo()
