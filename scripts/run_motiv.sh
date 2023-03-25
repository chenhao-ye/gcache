mkdir -p build-motiv
cd build-motiv
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
cd ..

cache_tick=2048  # 128 ticks

run_cmd() {
	wl=$1
	ws=$2
	theta=$3
	name=$4
	seed=2333

	result_dir=results_motiv/${name}
	mkdir -p ${result_dir}

	./build-motiv${sr}/gcache_bench_ghost --workload=${wl} --working_set=${ws} \
		--zipf_theta=${theta} --cache_tick=${cache_tick} --rand_seed=${seed}\
		--result_dir=${result_dir} > ${result_dir}/log
}

run_cmd zipf 1073741824 0.99 zipf_s1G_z0.99 &
run_cmd unif 1073741824 0    unif_s1G       &
run_cmd unif  751304704 0    unif_s0.7G     &
run_cmd unif  536870912 0    unif_s0.5G     &

wait
