#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pandas", "matplotlib", "seaborn", "numpy"]
# ///

import argparse
from pathlib import Path
import numpy as np
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

# Latency axis range, in log10(seconds). Defaults: 1 ns .. 10 ms.
LOG10_MIN_S = -7.5
LOG10_MAX_S = -2.5
BINS_PER_DECADE = 12

# Dark mode
BG = "#121622"
FG = "#e0e0e0"
GRID = "#30333e"
ROW_LABEL = "#f0f0f0"


def fmt_seconds(v: float) -> str:
    if v <= 0 or not np.isfinite(v):
        return ""
    if v < 1e-6:
        return f"{v * 1e9:g} ns"
    if v < 1e-3:
        return f"{v * 1e6:g} μs"
    if v < 1:
        return f"{v * 1e3:g} ms"
    return f"{v:g} s"


parser = argparse.ArgumentParser()
parser.add_argument("csv", nargs="?", default="result.csv")
args = parser.parse_args()
csv_path = Path(args.csv)
out_dir = csv_path.parent

df = pd.read_csv(csv_path, keep_default_na=False)
df["latency_ns"] = df["latency_ns"].astype(float)
df["latency_s"] = df["latency_ns"] * 1e-9
# guard against zero/negative for log axis
df = df[df["latency_s"] > 0].copy()

# preserve CSV method order (first-seen) for row order
method_order = list(df["method"].drop_duplicates())
method_color = {m: df[df["method"] == m]["color"].iloc[0] for m in method_order}

# seaborn's log_scale=True expects bin edges in log space, not data space
bins = np.linspace(
    LOG10_MIN_S,
    LOG10_MAX_S,
    int(round((LOG10_MAX_S - LOG10_MIN_S) * BINS_PER_DECADE)) + 1,
)

sns.set_theme(
    style="white",
    rc={
        "figure.facecolor": BG,
        "axes.facecolor": BG,
        "savefig.facecolor": BG,
        "text.color": FG,
        "axes.labelcolor": FG,
        "xtick.color": FG,
        "ytick.color": FG,
        "axes.edgecolor": GRID,
    },
)

g = sns.FacetGrid(
    df,
    row="method",
    row_order=method_order,
    aspect=9,
    height=1.2,
    sharex=True,
    sharey=False,
)


def plot_hist(data, **_):
    m = data["method"].iloc[0]
    sns.histplot(
        data=data,
        x="latency_s",
        bins=bins,
        log_scale=(True, False),
        stat="density",
        element="step",
        fill=True,
        alpha=0.75,
        linewidth=1.3,
        color=method_color[m],
    )


g.map_dataframe(plot_hist)


def add_row_label(values, **_):
    ax = plt.gca()
    ax.text(
        0.005,
        0.3,
        values.iloc[0],
        fontweight="bold",
        color=ROW_LABEL,
        ha="left",
        va="center",
        transform=ax.transAxes,
        fontsize=10,
        bbox=dict(facecolor=BG, edgecolor="none", alpha=0.7, pad=2),
    )


g.map(add_row_label, "method")

# transparent facets so slight overlap reads as a ridge
for ax in g.axes.flat:
    ax.set_facecolor((0, 0, 0, 0))
    ax.set_yticks([])
    ax.set_ylabel("")
    for spine in ("left", "right", "top"):
        ax.spines[spine].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.grid(True, which="major", axis="x", color=GRID, linewidth=0.5)
    ax.set_axisbelow(True)

# modest overlap
g.figure.subplots_adjust(hspace=-0.15)

# x-axis: decades 1ns..10s
bottom_ax = g.axes[-1][0]
bottom_ax.set_xlim(10.0**LOG10_MIN_S, 10.0**LOG10_MAX_S)
bottom_ax.set_xlabel("Latency")
bottom_ax.xaxis.set_major_locator(ticker.LogLocator(base=10, numticks=12))
bottom_ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: fmt_seconds(v)))
bottom_ax.xaxis.set_minor_locator(
    ticker.LogLocator(base=10, subs=tuple(range(2, 10)), numticks=100)
)
bottom_ax.xaxis.set_minor_formatter(ticker.NullFormatter())

g.set_titles("")
g.figure.suptitle(
    "Thread-to-thread communication latency",
    color=FG,
    fontsize=14,
    y=0.995,
)

out = out_dir / "chart_latency.svg"
g.figure.savefig(out, format="svg", facecolor=BG, bbox_inches="tight")
plt.close(g.figure)
print(f"Saved {out.name}")
