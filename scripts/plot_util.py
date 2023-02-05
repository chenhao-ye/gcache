import pandas as pd
import matplotlib.pyplot as plt
from typing import List, Dict, Callable

SUBFIG_WIDTH = 2.5
SUBFIG_HEIGHT = 2
MARKER_SIZE = 8


def get_subplots(nrows: int, ncols: int):
    fig, axes = plt.subplots(nrows=nrows, ncols=ncols)
    # fig.set_size_inches(ncols * SUBFIG_WIDTH, nrows * SUBFIG_HEIGHT)
    return fig, axes


def make_curve(ax,
               df: pd.DataFrame,
               x_col: str,
               y_col: str,
               x_range: List[int],
               filters: Dict,
               y_trans: Callable = lambda x: x,
               *,
               color=None,
               linestyle=None,
               marker=None,
               markersize=MARKER_SIZE,
               label=None):
    filter_df = df
    for fk, fv in filters.items():
        filter_df = filter_df[(filter_df[fk] == fv)]

    y_data = []
    for x_val in x_range:
        d = filter_df[(filter_df[x_col] == x_val)]
        if d.shape[0] != 1:
            raise ValueError(f"Unexpected data: ({x_col}={x_val}): "
                             f"shape {d.shape}")
        y_data.append(y_trans(d.head(1)[y_col]))
    ax.plot(x_range,
            y_data,
            color=color,
            linestyle=linestyle,
            marker=marker,
            markersize=markersize,
            label=label)
