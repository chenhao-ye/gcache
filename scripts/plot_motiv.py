import pandas as pd
import numpy as np
import math
import matplotlib.pyplot as plt
from typing import List, Tuple


def get_subplots(nrows: int, ncols: int):
    fig, axes = plt.subplots(nrows=nrows, ncols=ncols)
    return fig, axes


def load_data(name: str):
    ghost_df = pd.read_csv(f"results/{name}/hit_rate_ghost.csv",
                           na_values="nan",
                           skipinitialspace=True)
    sample_df = pd.read_csv(f"results/{name}/hit_rate_sample.csv",
                            na_values="nan",
                            skipinitialspace=True)
    return ghost_df, sample_df


def get_motiv(df: pd.DataFrame) -> Tuple[List[float], List[float]]:
    data = df.values.tolist()
    size_list, motiv_list = [], []
    window_len = 2
    for i in range(window_len, len(data) - window_len):
        left_size, left_hit_rate = data[i - window_len]
        right_size, right_hit_rate = data[i + window_len]
        curr_size, curr_hit_rate = data[i]
        left_miss_rate = 1 - left_hit_rate
        right_miss_rate = 1 - right_hit_rate
        curr_miss_rate = 1 - curr_hit_rate
        deriv = (right_miss_rate - left_miss_rate) / (right_size - left_size)
        size_list.append(curr_size / (1024 * 1024 * 1024 / 4096))  # unit: GB
        motiv_list.append(-deriv / curr_miss_rate if curr_miss_rate != 0 \
            else math.nan)
    return size_list, motiv_list


if __name__ == "__main__":
    fig, ax = get_subplots(1, 1)
    for name in [
            "zipf_s1G_z0.99", "unif_s1G", "unif_s0.75G", "unif_s0.5G",
            "unif_s0.25G", "seq_s2G"
    ]:
        ghost_df, _ = load_data(name)
        x, y = get_motiv(ghost_df)
        ax.plot(x, y, label=name)
        print(f"{name}: size:  {x}")
        print(f"{name}: motiv: {y}")
    ax.set_ylim([0, 0.00004])
    ax.set_xlabel("Cache size (GB)")
    ax.set_ylabel("-m'/m")
    ax.legend()
    fig.savefig("motiv.pdf")
