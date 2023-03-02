import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from typing import List, Dict, Callable, Optional

SUBFIG_WIDTH = 3
SUBFIG_HEIGHT = 2.5
MARKER_SIZE = 8


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
               y_trans: Callable = lambda x: x,
               df_err: Optional[pd.DataFrame] = None,
               *,
               color=None,
               linestyle=None,
               marker=None,
               markersize=MARKER_SIZE,
               label=None):
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
                color=color,
                linestyle=linestyle,
                marker=marker,
                markersize=markersize,
                label=label)
    else:
        ax.errorbar(x_range,
                    y_data,
                    yerr=y_err,
                    color=color,
                    linestyle=linestyle,
                    marker=marker,
                    markersize=markersize,
                    label=label)
