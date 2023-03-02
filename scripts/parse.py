import logging
import os
import pandas as pd

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
    if df_all is None:
        df_all = df
    else:
        df_all = pd.concat([df_all, df])

df_raw = df_all.sort_values(
    by=["sample_shift", "workload", "num_blocks", "zipf_theta"])
df_raw.to_csv(f"{results_dir}/perf_raw.csv")

df_group = df_all.groupby(
    by=["sample_shift", "workload", "num_blocks", "zipf_theta"])
df_group.mean().to_csv(f"{results_dir}/perf_mean.csv")
df_group.std().to_csv(f"{results_dir}/perf_std.csv")
