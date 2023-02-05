import logging
import os
import pandas as pd

results_dir = "./results"

df_all = None

for subdir in os.listdir(results_dir):
    if not subdir.startswith("sr"):
        logging.warning(f"Unknown subdirectory: {results_dir}/{subdir}")
        continue
    df = pd.read_csv(
        f"{results_dir}/{subdir}/perf.csv",
        header=0,
        index_col=["sample_shift", "workload", "num_blocks", "zipf_theta"],
        skipinitialspace=True)
    if df_all is None:
        df_all = df
    else:
        df_all = pd.concat([df_all, df])

df_all = df_all.sort_values(
    by=["sample_shift", "workload", "num_blocks", "zipf_theta"])
df_all.to_csv(f"{results_dir}/perf_data.csv")
print(df_all)