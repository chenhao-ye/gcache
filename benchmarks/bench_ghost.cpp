// A bench process will evaluate 1) accuracy of sampled ghost cache
// 2) performance

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>

#include "gcache/ghost_cache.h"
#include "workload.h"

// use compile-time macro to set this
#ifndef SAMPLE_SHIFT
#define SAMPLE_SHIFT 5
#endif

static OffsetType wl_type = OffsetType::ZIPF;
static uint64_t num_blocks = 1024 * 1024 * 1024 / 4096;  // 1 GB
static uint64_t num_ops = 10'000'000;                    // 10M
static uint64_t preheat_num_ops = num_ops / 10;
static double zipf_theta = 0.99;

static uint32_t cache_tick = num_blocks / 32;
static uint32_t cache_min = cache_tick;
static uint32_t cache_max = num_blocks;

static std::filesystem::path result_dir = ".";

static bool run_ghost = true;
static bool run_sample = true;

void parse_args(int argc, char* argv[]) {
  char junk;
  uint64_t n;
  double f;
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "--workload=", 11) == 0) {
      if (strcmp(argv[i] + 11, "zipf") == 0) {
        wl_type = OffsetType::ZIPF;
      } else if (strcmp(argv[i] + 11, "unif") == 0) {
        wl_type = OffsetType::UNIF;
      } else if (strcmp(argv[i] + 11, "seq") == 0) {
        wl_type = OffsetType::SEQ;
      } else {
        std::cerr << "Invalid argument: Unrecognized workload: " << argv[i] + 11
                  << std::endl;
        exit(1);
      }
    } else if (strncmp(argv[i], "--result_dir=", 13) == 0) {
      result_dir = argv[i] + 13;
      if (!std::filesystem::is_directory(result_dir)) {
        std::cerr << "Invalid argument: result_dir is not a valid directory: "
                  << result_dir << std::endl;
        exit(1);
      }
    } else if (sscanf(argv[i], "--working_set=%ld%c", &n, &junk) == 1) {
      num_blocks = n / 4096;  // this is just a shortcut for num_blocks
    } else if (sscanf(argv[i], "--num_blocks=%ld%c", &n, &junk) == 1) {
      num_blocks = n;
    } else if (sscanf(argv[i], "--num_ops=%ld%c", &n, &junk) == 1) {
      num_ops = n;
      preheat_num_ops = num_ops / 10;
    } else if (sscanf(argv[i], "--zipf_theta=%lf%c", &f, &junk) == 1) {
      zipf_theta = f;
    } else if (sscanf(argv[i], "--cache_tick=%ld%c", &n, &junk) == 1) {
      cache_tick = n;
    } else if (sscanf(argv[i], "--cache_min=%ld%c", &n, &junk) == 1) {
      cache_min = n;
    } else if (sscanf(argv[i], "--cache_max=%ld%c", &n, &junk) == 1) {
      cache_max = n;
    } else if (strcmp(argv[i], "--no_ghost") == 0) {
      run_ghost = false;
    } else if (strcmp(argv[i], "--no_sampled") == 0) {
      run_sample = false;
    } else {
      std::cerr << "Invalid argument: " << argv[i] << std::endl;
      exit(1);
    }
  }
  if (cache_min > cache_max) {
    std::cerr << "Invalid cache configs: cache_min > cache_max" << std::endl;
    exit(1);
  }
  if ((cache_max - cache_min) % cache_tick != 0) {
    std::cerr << "Invalid cache configs: Invalid cache_tick" << std::endl;
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  parse_args(argc, argv);

  // we dump all config and data into a csv file for parser
  std::ofstream ofs_perf(result_dir / "perf.csv");
  ofs_perf << "workload,num_blocks,num_ops,zipf_theta,cache_tick,cache_min,"
              "cache_max,sample_shift,baseline_us,ghost_us,sampled_us,"
              "ghost_cost_uspop,sampled_cost_uspop,avg_err,max_err\n";

  std::cout << "Config: wl_type=";
  switch (wl_type) {
    case OffsetType::SEQ:
      std::cout << "seq";
      ofs_perf << "seq";
      break;
    case OffsetType::UNIF:
      std::cout << "unif";
      ofs_perf << "unif";
      break;
    case OffsetType::ZIPF:
      std::cout << "zipf";
      ofs_perf << "zipf";
      break;
    default:
      throw std::runtime_error("Unimplemented offset wl_type");
  }
  std::cout << ", num_blocks=" << num_blocks << ", num_ops=" << num_ops
            << ", zipf_theta=" << zipf_theta << ", cache_tick=" << cache_tick
            << ", cache_min=" << cache_min << ", cache_max=" << cache_max
            << ", sample_shift=" << SAMPLE_SHIFT << std::endl;
  ofs_perf << ',' << num_blocks << ',' << num_ops << ',' << zipf_theta << ','
           << cache_tick << ',' << cache_min << ',' << cache_max << ','
           << SAMPLE_SHIFT;

  Offsets offsets1(num_ops, wl_type, num_blocks, 1, zipf_theta);
  Offsets offsets2(num_ops, wl_type, num_blocks, 1, zipf_theta);
  Offsets offsets3(num_ops, wl_type, num_blocks, 1, zipf_theta);

  uint64_t offset_checksum1 = 0, offset_checksum2 = 0, offset_checksum3 = 0;

  gcache::GhostCache ghost_cache(cache_tick, cache_min, cache_max);
  gcache::SampleGhostCache<SAMPLE_SHIFT> sample_ghost_cache(
      cache_tick, cache_min, cache_max);

  // preheat: run a subset of stream to populate the cache
  Offsets prehead_offsets(preheat_num_ops, wl_type, num_blocks, 1, zipf_theta,
                          /*seed*/ 0x736);
  auto preheat_begin_ts = std::chrono::high_resolution_clock::now();
  for (auto off : prehead_offsets) {
    if (run_ghost) ghost_cache.access(off);
    if (run_sample) sample_ghost_cache.access(off);
  }
  auto preheat_end_ts = std::chrono::high_resolution_clock::now();
  ghost_cache.reset_stat();
  sample_ghost_cache.reset_stat();
  // for human's reference only, not used for any data processing purposes
  std::cout << "Preheat completes in "
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   preheat_end_ts - preheat_begin_ts)
                       .count() /
                   1000.0
            << " sec" << std::endl;

  // start benchmarking
  auto t0 = std::chrono::high_resolution_clock::now();
  for (auto off : offsets1) {
    // prevent compiler optimizing this out...)
    offset_checksum1 ^= off;
  }

  auto t1 = std::chrono::high_resolution_clock::now();
  if (run_ghost) {
    for (auto off : offsets2) {
      offset_checksum2 ^= off;
      ghost_cache.access(off);
    }
  }

  auto t2 = std::chrono::high_resolution_clock::now();
  if (run_sample) {
    for (auto off : offsets3) {
      offset_checksum3 ^= off;
      sample_ghost_cache.access(off);
    }
  }
  auto t3 = std::chrono::high_resolution_clock::now();

  uint64_t t_base = 0, t_ghost = 0, t_sample = 0;
  double ghost_overhead = 0, sampled_overhead = 0;
  t_base =
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  if (run_ghost) {
    t_ghost =
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    ghost_overhead = double(t_ghost - t_base) / num_ops;
  }
  if (run_sample) {
    t_sample =
        std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    sampled_overhead = double(t_sample - t_base) / num_ops;
  }

  std::cout << "Baseline:            " << t_base << " us\n";
  std::cout << "Ghost Cache:         " << t_ghost << " us\n";
  std::cout << "Sampled Ghost Cache: " << t_sample << " us\n";
  std::cout << "Ghost Overhead:      " << ghost_overhead << " us/op\n";
  std::cout << "Sampled Overhead:    " << sampled_overhead << " us/op\n";
  ofs_perf << ',' << t_base << ',' << t_ghost << ',' << t_sample << ','
           << ghost_overhead << ',' << sampled_overhead;

  double avg_err = 0, max_err = 0;

  if (run_ghost) {
    if (offset_checksum1 != offset_checksum2)
      std::cerr << "WARNING: offset checksums mismatch; "
                   "random generator may not be deterministic!\n";

    std::ofstream ofs_ghost(result_dir / "hit_rate_ghost.csv");
    ofs_ghost << "num_blocks,hit_rate\n";
    for (size_t i = cache_min; i <= cache_max; i += cache_tick)
      ofs_ghost << i << ',' << ghost_cache.get_hit_rate(i) << '\n';
  }

  if (run_sample) {
    if (offset_checksum2 != offset_checksum3)
      std::cerr << "WARNING: offset checksums mismatch; "
                   "random generator may not be deterministic!\n";

    std::ofstream ofs_sample(result_dir / "hit_rate_sample.csv");
    ofs_sample << "num_blocks,hit_rate\n";
    for (size_t i = cache_min; i <= cache_max; i += cache_tick)
      ofs_sample << i << ',' << sample_ghost_cache.get_hit_rate(i) << '\n';
  }

  if (run_ghost && run_sample) {
    std::vector<double> hit_rate_diff;  // dump the ghost cache status
    for (size_t i = cache_min; i <= cache_max; i += cache_tick) {
      double hr1 = ghost_cache.get_hit_rate(i);
      double hr2 = sample_ghost_cache.get_hit_rate(i);
      hit_rate_diff.emplace_back(std::abs(hr1 - hr2));
    }
    // mean absolute error (MAE)
    avg_err = std::accumulate(hit_rate_diff.begin(), hit_rate_diff.end(), 0.0) /
              hit_rate_diff.size();
    max_err = *std::max_element(hit_rate_diff.begin(), hit_rate_diff.end());
  }

  std::cout << "Avg Error: " << avg_err << std::endl;
  std::cout << "Max Error: " << max_err << std::endl;
  ofs_perf << ',' << avg_err << ',' << max_err << std::endl;

  return 0;
}
