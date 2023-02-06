set -e  # stop if any command fails

cache_tick=4096 # 64 ticks

run_cmd() {
	wl=$1
	ws=$2
	theta=$3
	name=$4

	mkdir -p results/sr${sr}_${name}

	./build-SR${sr}/gcache_bench_ghost --workload=${wl} --working_set=${ws} \
		--zipf_theta=${theta} --cache_tick=${cache_tick} \
		--result_dir=results/sr${sr}_${name} > results/sr${sr}_${name}/log
}

for sr in 3 4 5 6 7 8; do
	echo "Compile with sample rate: ${sr}"
	mkdir -p build-SR${sr}
	cd build-SR${sr}
	cmake -DSAMPLE_SHIFT=${sr} ..
	make -j
	cd ..
	echo "Run with sample rate: ${sr}"

	run_cmd zipf 1073741824 0.99 zipf_s1G_z0.99
	run_cmd zipf 1073741824 0.5  zipf_s1G_z0.5
	run_cmd unif 2147483648 0    unif_s2G
	run_cmd unif 1073741824 0    unif_s1G
	run_cmd unif 805306368  0    unif_s0.75G
	run_cmd unif 536870912  0    unif_s0.5G
	run_cmd unif 268435456  0    unif_s0.25G
done
