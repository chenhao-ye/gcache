import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from typing import List, Dict, Callable, Optional

SUBFIG_WIDTH = 2.5
SUBFIG_HEIGHT = 1.8
MARKER_SIZE = 4
LEGEND_MARKER_SIZE = 5

plt.rcParams['xtick.major.pad'] = '2'
plt.rcParams['ytick.major.pad'] = '2'
plt.rcParams['xtick.major.size'] = '2.5'
plt.rcParams['ytick.major.size'] = '2.5'
plt.rcParams['axes.labelpad'] = '1'

color_map = {
    "zipf_s1G_z0.99": "0.5",
    "unif_s1G": "0",
    "zipf_s2G_z0.5": "0.8",
    "unif_s0.5G": "0.4",
    "unif_s0.7G": "0.2",
}

marker_map = {
    "zipf_s1G_z0.99": "o",
    "unif_s1G": "d",
    "zipf_s2G_z0.5": "^",
}

linestyle_map = {
    "zipf_s1G_z0.99": "-",
    "unif_s1G": "-",
    "zipf_s2G_z0.5": "-",
    "unif_s0.5G": ":",
    "unif_s0.7G": "-.",
}

label_map = {
    "zipf_s1G_z0.99": "Zipf (theta 0.99), 1GB",
    "unif_s1G": "Unif, 1GB",
    "zipf_s2G_z0.5": "Zipf (theta 0.5), 2GB",
    "unif_s0.5G": "Unif, 0.5GB",
    "unif_s0.7G": "Unif, 0.7GB",
}


def get_subplots(nrows: int, ncols: int):
    fig, axes = plt.subplots(nrows=nrows, ncols=ncols)
    fig.set_size_inches(ncols * SUBFIG_WIDTH, nrows * SUBFIG_HEIGHT)
    return fig, axes


def make_curve(ax,
               df: pd.DataFrame,
               x_col: str,
               y_col: str,
               x_range: List[int],
               filters: Dict,
               name,
               y_trans: Callable = lambda x: x,
               df_err: Optional[pd.DataFrame] = None,
               markersize=MARKER_SIZE):
    filter_df = df
    filter_df_err = df_err
    for fk, fv in filters.items():
        filter_df = filter_df[(filter_df[fk] == fv)]
        if df_err is not None:
            filter_df_err = filter_df_err[(filter_df_err[fk] == fv)]

    y_data = []
    y_err = []
    for x_val in x_range:
        d = filter_df[(filter_df[x_col] == x_val)]
        if d.shape[0] != 1:
            raise ValueError(f"Unexpected data: ({x_col}={x_val}): "
                             f"shape {d.shape}")
        y_data.append(y_trans(d.head(1)[y_col].values[0]))
        if df_err is None:
            continue
        d = filter_df_err[(filter_df_err[x_col] == x_val)]
        if d.shape[0] != 1:
            raise ValueError(f"Unexpected data (err bar): ({x_col}={x_val}): "
                             f"shape {d.shape}")
        y_err.append(y_trans(d.head(1)[y_col].values[0]))
    if df_err is None:
        ax.plot(x_range,
                y_data,
                color=color_map[name],
                linestyle=linestyle_map[name],
                marker=marker_map[name],
                markersize=markersize,
                label=label_map[name])
    else:
        ax.errorbar(x_range,
                    y_data,
                    yerr=y_err,
                    color=color_map[name],
                    capsize=2,
                    linestyle=linestyle_map[name],
                    marker=marker_map[name],
                    markersize=markersize,
                    label=label_map[name])


def make_legend(keys: List[str],
                width=5,
                height=0.15,
                ncol=None,
                fontsize=10,
                columnspacing=1,
                labelspacing=0.4):
    if ncol is None:
        ncol = len(keys)
    pseudo_fig = plt.figure()
    ax = pseudo_fig.add_subplot(111)

    lines = []
    for k in keys:
        line, = ax.plot([], [],
                        color=color_map[k],
                        linestyle=linestyle_map[k],
                        marker=marker_map.get(k),
                        markersize=LEGEND_MARKER_SIZE,
                        label=label_map[k])
        lines.append(line)

    legend_fig = plt.figure()
    legend_fig.set_size_inches(width, height)
    legend_fig.legend(lines, [label_map[k] for k in keys],
                      loc='center',
                      ncol=ncol,
                      fontsize=fontsize,
                      frameon=False,
                      columnspacing=columnspacing,
                      labelspacing=labelspacing)
    legend_fig.set_tight_layout({"pad": 0, "w_pad": 0, "h_pad": 0})
    return legend_fig
