// A bench process will evaluate 1) accuracy of sampled ghost cache
// 2) performance

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>

#include "gcache/ghost_cache.h"
#include "workload.h"

constexpr static OffsetType type = OffsetType::ZIPF;
constexpr static uint64_t size = 1024 * 1024 * 1024 / 4096;  // 1 GB
constexpr static uint64_t num_ops = 1'000'000'000;
constexpr static uint64_t preheat_num_ops = num_ops / 10;
constexpr static double zipf_theta = 0.99;

constexpr const uint32_t cache_tick = size / 32;
constexpr const uint32_t cache_min = cache_tick;
constexpr const uint32_t cache_max = size;
constexpr const uint32_t sample_rate = 5;

int main() {
  std::cout << "Config: type=";
  switch (type) {
    case OffsetType::SEQ:
      std::cout << "seq";
      break;
    case OffsetType::UNIF:
      std::cout << "unif";
      break;
    case OffsetType::ZIPF:
      std::cout << "zipf";
      break;
    default:
      throw std::runtime_error("Unimplemented offset type");
  }
  std::cout << ", size=" << size << ", num_ops=" << num_ops
            << ", zipf_theta=" << zipf_theta << ", cache_tick=" << cache_tick
            << ", cache_min=" << cache_min << ", cache_max=" << cache_max
            << ", sample_rate=" << sample_rate << std::endl;

  Offsets offsets1(num_ops, type, size, 1, zipf_theta);
  Offsets offsets2(num_ops, type, size, 1, zipf_theta);
  Offsets offsets3(num_ops, type, size, 1, zipf_theta);

  uint64_t offset_checksum1 = 0, offset_checksum2 = 0, offset_checksum3 = 0;

  gcache::GhostCache ghost_cache(cache_tick, cache_min, cache_max);
  gcache::SampleGhostCache<sample_rate> sample_ghost_cache(
      cache_tick, cache_min, cache_max);

  // preheat: run a subset of stream to populate the cache
  Offsets prehead_offsets(preheat_num_ops, type, size, 1, zipf_theta,
                          /*seed*/ 0x736);
  auto preheat_begin_ts = std::chrono::high_resolution_clock::now();
  for (auto off : prehead_offsets) {
    ghost_cache.access(off);
    sample_ghost_cache.access(off);
  }
  auto preheat_end_ts = std::chrono::high_resolution_clock::now();
  ghost_cache.reset_stat();
  sample_ghost_cache.reset_stat();
  std::cout << "Preheat completes in "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   preheat_end_ts - preheat_begin_ts)
                   .count()
            << " us" << std::endl;

  // start benchmarking
  auto t0 = std::chrono::high_resolution_clock::now();
  for (auto off : offsets1) {
    // prevent compiler optimizing this out...)
    offset_checksum1 ^= off;
  }

  auto t1 = std::chrono::high_resolution_clock::now();
  for (auto off : offsets2) {
    offset_checksum2 ^= off;
    ghost_cache.access(off);
  }

  auto t2 = std::chrono::high_resolution_clock::now();
  for (auto off : offsets3) {
    offset_checksum3 ^= off;
    sample_ghost_cache.access(off);
  }
  auto t3 = std::chrono::high_resolution_clock::now();

  if (offset_checksum1 != offset_checksum2 ||
      offset_checksum2 != offset_checksum3) {
    std::cerr << "WARNING: offset checksums mismatch; random generator may not "
                 "be deterministic!\n";
  }

  uint64_t t_base =
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  uint64_t t_ghost =
      std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
  uint64_t t_sample =
      std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

  std::cout << "Baseline:            " << t_base << " us\n";
  std::cout << "Ghost Cache:         " << t_ghost << " us\n";
  std::cout << "Sampled Ghost Cache: " << t_sample << " us\n";

  std::cout << "Ghost Overhead:      " << double(t_ghost - t_base) / num_ops
            << " us/op\n";
  std::cout << "Sampled Overhead:    " << double(t_sample - t_base) / num_ops
            << " us/op\n";

  return 0;
}
