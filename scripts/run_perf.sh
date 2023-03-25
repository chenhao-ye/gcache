set -e  # stop if any command fails

cache_tick=8192  # 32 ticks

run_cmd() {
	wl=$1
	ws=$2
	theta=$3
	name=$4
	seed=$5

	result_dir=results/sr${sr}_${name}_sd${seed}
	mkdir -p ${result_dir}

	./build-SR${sr}/gcache_bench_ghost --workload=${wl} --working_set=${ws} \
		--zipf_theta=${theta} --cache_tick=${cache_tick} --rand_seed=${seed}\
		--result_dir=${result_dir} > ${result_dir}/log
}

for sr in 3 4 5 6 7 8; do
	echo "Compile with sample rate: ${sr}"
	mkdir -p build-SR${sr}
	cd build-SR${sr}
	cmake -DSAMPLE_SHIFT=${sr} -DCMAKE_BUILD_TYPE=Release ..
	make -j
	cd ..
done

for seed in {2048..2347}; do  # 300 times
	for sr in 3 4 5 6 7 8; do
		echo "Run with sample rate: ${sr}, seed=${seed}"
		run_cmd zipf 1073741824 0.99 zipf_s1G_z0.99 ${seed}
		run_cmd zipf 2147483648 0.5  zipf_s2G_z0.5  ${seed}
		run_cmd unif 1073741824 0    unif_s1G       ${seed}
	done
done
