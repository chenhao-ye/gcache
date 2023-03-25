import pandas as pd
import math
import matplotlib.pyplot as plt
from typing import List, Tuple
from plot_util import *

results_dir = "results_motiv"


def load_hit_rate_data(name: str):
    ghost_df = pd.read_csv(f"{results_dir}/{name}/hit_rate_ghost.csv",
                           na_values="nan",
                           skipinitialspace=True)
    sampled_df = pd.read_csv(f"{results_dir}/{name}/hit_rate_sampled.csv",
                             na_values="nan",
                             skipinitialspace=True)
    return ghost_df, sampled_df


def get_motiv(df: pd.DataFrame) -> Tuple[List[float], List[float]]:
    data = df.values.tolist()
    size_list, motiv_list = [], []
    window_len = 2
    blk2GB = lambda b: b / (256 * 1024)
    for i in range(window_len, len(data) - window_len):
        left_size, left_hit_rate = data[i - window_len]
        right_size, right_hit_rate = data[i + window_len]
        curr_size, curr_hit_rate = data[i]
        left_miss_rate = 1 - left_hit_rate
        right_miss_rate = 1 - right_hit_rate
        curr_miss_rate = 1 - curr_hit_rate
        deriv = (right_miss_rate - left_miss_rate) / \
            blk2GB(right_size - left_size)
        size_list.append(blk2GB(curr_size))
        motiv_list.append(-deriv / curr_miss_rate if curr_miss_rate != 0 \
            else math.nan)
    return size_list, motiv_list


def plot_miss_rate(ax):
    for name in [
            "zipf_s1G_z0.99",
            "unif_s0.5G",
            "unif_s0.7G",
            "unif_s1G",
    ]:
        ghost_df, _ = load_hit_rate_data(name)
        ghost_df["miss_rate"] = (1 - ghost_df["hit_rate"]) * 100
        ghost_df["size"] = ghost_df["num_blocks"] / (256 * 1024)
        ax.plot(ghost_df["size"],
                ghost_df["miss_rate"],
                label=label_map[name],
                color=color_map[name],
                linestyle=linestyle_map[name])

    xticks = [0, 0.25, 0.5, 0.75, 1]
    yticks = [0, 25, 50, 75, 100]

    ax.set_xticks(xticks, [f"{t}" for t in xticks])
    ax.set_xlim((0, 1))

    ax.set_yticks(yticks, [f"{t:g}" for t in yticks], rotation=90)
    ax.set_ylim([0, 100])
    ax.set_xlabel("Cache size (GB)")
    ax.set_ylabel("Miss rate (%)")


def plot_motiv(ax):
    for name in [
            "zipf_s1G_z0.99",
            "unif_s0.5G",
            "unif_s0.7G",
            "unif_s1G",
    ]:
        ghost_df, _ = load_hit_rate_data(name)
        x, y = get_motiv(ghost_df)
        ax.plot(x,
                y,
                label=label_map[name],
                color=color_map[name],
                linestyle=linestyle_map[name])

    xticks = [0, 0.25, 0.5, 0.75, 1]
    yticks = [0, 2, 4, 6, 8]

    ax.set_xticks(xticks, [f"{t}" for t in xticks])
    ax.set_xlim((0, 1))

    ax.set_yticks(yticks, [f"{t:g}" for t in yticks], rotation=90)
    ax.set_ylim((0, 8))
    ax.set_xlabel("Cache size (GB)")
    ax.set_ylabel("M = -m'/m (GB^-1)")


if __name__ == "__main__":
    fig, (ax_mr, ax_motiv) = get_subplots(nrows=1, ncols=2)
    plot_miss_rate(ax_mr)
    plot_motiv(ax_motiv)

    fig.set_tight_layout({"pad": 0.05, "w_pad": 0.05, "h_pad": 0.05})
    print("Save figure as `motiv.pdf`")
    fig.savefig("motiv.pdf")

    legend_fig = make_legend(
        ["zipf_s1G_z0.99", "unif_s0.5G", "unif_s0.7G", "unif_s1G"],
        width=6,
        columnspacing=0.9,
        labelspacing=0.2)
    legend_fig.savefig("motiv_legend.pdf")
