import pandas as pd
import math
import matplotlib.pyplot as plt
from typing import List, Tuple
from plot_util import *

results_dir = "results"
sample_shift_list = [3, 4, 5, 6, 7, 8]

if __name__ == "__main__":
    fig, (ax_err, ax_cost) = get_subplots(nrows=1, ncols=2)
    df = pd.read_csv(f"{results_dir}/perf_data.csv", header=0, index_col=False)

    for wl, ws, theta, name in [
        ("zipf", 1024 * 1024 * 1024 / 4096, 0.99, "zipf,1GB,theta=0.99"),
        ("zipf", 1024 * 1024 * 1024 / 4096, 0.5, "zipf,1GB,theta=0.5"),
        ("unif", 1024 * 1024 * 1024 / 4096, 0, "unif,1GB"),
        ("unif", 1024 * 1024 * 1024 / 4096 / 2, 0, "unif,0.5GB"),
    ]:
        make_curve(
            ax_err,
            df,
            x_col="sample_shift",
            y_col="avg_err",
            x_range=sample_shift_list,
            filters={
                "workload": wl,
                "num_blocks": ws,
                "zipf_theta": theta,
            },
            y_trans=lambda x: x * 100,  # unit: %
            label=name)
        make_curve(
            ax_cost,
            df,
            x_col="sample_shift",
            y_col="sampled_cost_uspop",
            x_range=sample_shift_list,
            filters={
                "workload": wl,
                "num_blocks": ws,
                "zipf_theta": theta,
            },
            y_trans=lambda x: x * 1000,  # unit: us -> ns
            label=name)
    ax_err.set_xticks(sample_shift_list,
                      [f"1/{2**x}" for x in sample_shift_list])
    ax_err.set_xlabel("Sample rate")
    ax_cost.set_xticks(sample_shift_list,
                       [f"1/{2**x}" for x in sample_shift_list])
    ax_cost.set_xlabel("Sample rate")

    ax_err.set_ylabel("Error rate (%)")
    ax_cost.set_ylabel("Overhead (ns/op)")

    ax_cost.legend()

    fig.set_tight_layout({"pad": 0.1, "w_pad": 0.1, "h_pad": 0.1})
    fig.savefig("perf.pdf")