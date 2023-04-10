import pandas as pd
import math
import matplotlib
import matplotlib.pyplot as plt
from typing import List, Tuple
from plot_util import *

results_dir = "results"
sample_shift_list = [3, 4, 5, 6, 7, 8]

matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

if __name__ == "__main__":
    fig, (ax_err, ax_cost) = get_subplots(nrows=1, ncols=2)
    ax_err.spines[['right', 'top']].set_visible(False)
    ax_cost.spines[['right', 'top']].set_visible(False)
    df_mean = pd.read_csv(f"{results_dir}/perf_mean.csv",
                          header=0,
                          index_col=False)
    df_std = pd.read_csv(f"{results_dir}/perf_std.csv",
                         header=0,
                         index_col=False)

    for wl, ws, theta, name in [
        ("zipf", 1024 * 1024 * 1024 / 4096, 0.99, "zipf_s1G_z0.99"),
        ("unif", 1024 * 1024 * 1024 / 4096, 0, "unif_s1G"),
        ("zipf", 2 * 1024 * 1024 * 1024 / 4096, 0.5, "zipf_s2G_z0.5"),
    ]:
        make_curve(
            ax_err,
            df_mean,
            x_col="sample_shift",
            y_col="avg_err",
            x_range=sample_shift_list,
            filters={
                "workload": wl,
                "num_blocks": ws,
                "zipf_theta": theta,
            },
            y_trans=lambda x: x * 100,  # unit: %
            df_err=df_std,
            name=name)
        make_curve(
            ax_cost,
            df_mean,
            x_col="sample_shift",
            y_col="sampled_cost_uspop",
            x_range=sample_shift_list,
            filters={
                "workload": wl,
                "num_blocks": ws,
                "zipf_theta": theta,
            },
            y_trans=lambda x: x * 1000,  # unit: us -> ns
            df_err=df_std,
            name=name)
    ax_err.set_ylim([0, 4])
    ax_cost.set_ylim([0, 100])

    ax_err.set_xticks(sample_shift_list,
                      [f"1/{2**x}" for x in sample_shift_list],
                      fontsize=8)
    ax_err.set_xlabel("Sample rate")
    ax_cost.set_xticks(sample_shift_list,
                       [f"1/{2**x}" for x in sample_shift_list],
                       fontsize=8)
    ax_cost.set_xlabel("Sample rate")

    ticks_err = [0, 1, 2, 3, 4]
    ticks_cost = [0, 25, 50, 75, 100]
    for ax, ticks in [(ax_err, ticks_err), (ax_cost, ticks_cost)]:
        ax.set_yticks(ticks, [f"{t}" for t in ticks], rotation=90)

    ax_err.set_ylabel("Error rate (%)")
    ax_cost.set_ylabel("Overhead (ns/block)")

    fig.set_tight_layout({"pad": 0.05, "w_pad": 0.05, "h_pad": 0.05})
    print("Save figure as `ghost_perf.png`")
    fig.savefig("ghost_perf.png")
    print("Save figure as `ghost_perf.pdf`")
    fig.savefig("ghost_perf.pdf")
    plt.close(fig)

    print("Save legend as `ghost_perf_legend.pdf`")
    legend_fig = make_legend(["zipf_s1G_z0.99", "unif_s1G", "zipf_s2G_z0.5"])

    print("Legend saved to `ghost_perf_legend.jpg`")
    legend_fig.savefig("ghost_perf_legend.jpg")
    print("Legend saved to `ghost_perf_legend.pdf")
    legend_fig.savefig("ghost_perf_legend.pdf")
    plt.close(legend_fig)
