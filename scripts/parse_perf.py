import logging
import os
import pandas as pd


def parse():
    results_dir = "results"

    df_all = None

    for subdir in os.listdir(results_dir):
        if not subdir.startswith("sr"):
            logging.warning(f"Unknown subdirectory: {results_dir}/{subdir}")
            continue
        df = pd.read_csv(f"{results_dir}/{subdir}/perf.csv",
                         header=0,
                         index_col=[
                             "sample_shift", "workload", "num_blocks",
                             "zipf_theta", "rand_seed"
                         ],
                         skipinitialspace=True)
        df_all = df if df_all is None else pd.concat([df_all, df])

    df_all["ghost_cost_us_per_op"] = \
        (df_all["ghost_us"] - df_all["baseline_us"]) / df_all["num_ops"]
    df_all["sampled_cost_us_per_op"] = \
        (df_all["sampled_us"] - df_all["baseline_us"]) / df_all["num_ops"]

    df_raw = df_all.sort_values(
        by=["sample_shift", "workload", "num_blocks", "zipf_theta"])
    df_raw.to_csv(f"{results_dir}/perf_raw.csv")

    df_group = df_all.groupby(
        by=["sample_shift", "workload", "num_blocks", "zipf_theta"])

    df_mean = df_group.mean().reset_index()
    df_std = df_group.std().reset_index()

    df_mean.to_csv(f"{results_dir}/perf_mean.csv")
    df_std.to_csv(f"{results_dir}/perf_std.csv")

    return df_mean, df_std


if __name__ == "__main__":
    parse()
