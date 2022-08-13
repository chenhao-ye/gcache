#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>

#include "gcache/ghost_cache.h"
#include "gcache/handle.h"
#include "util.h"

using namespace gcache;
constexpr const uint32_t bench_size = 256 * 1024;
constexpr const uint32_t large_bench_size = 1024 * 1024;

void test1() {
  GhostCache ghost_cache(1, 3, 6);
  std::cout << "=== Test 1 ===\n";

  ghost_cache.access(0);
  ghost_cache.access(1);
  ghost_cache.access(2);
  ghost_cache.access(3);
  std::cout << "Expect: Boundaries: [1, 0, (null)]; Stat: [0/4, 0/4, 0/4]\n";
  std::cout << ghost_cache;

  ghost_cache.access(4);
  ghost_cache.access(5);
  std::cout << "Expect: Boundaries: [3, 2, 1]; Stat: [0/6, 0/6, 0/6]\n";
  std::cout << ghost_cache;

  ghost_cache.access(2);
  std::cout << "Expect: Boundaries: [4, 3, 1]; Stat: [0/7, 1/7, 1/7]\n";
  std::cout << ghost_cache;

  ghost_cache.access(4);
  std::cout << "Expect: Boundaries: [5, 3, 1]; Stat: [1/8, 2/8, 2/8]\n";
  std::cout << ghost_cache;
}

void test2() {
  GhostCache ghost_cache(2, 2, 7);
  std::cout << "=== Test 2 ===\n";

  ghost_cache.access(0);
  ghost_cache.access(1);
  ghost_cache.access(2);
  ghost_cache.access(3);
  std::cout << "Expect: Boundaries: [2, 0, (null)]; Stat: [0/4, 0/4, 0/4]\n";
  std::cout << ghost_cache;

  ghost_cache.access(4);
  ghost_cache.access(5);
  std::cout << "Expect: Boundaries: [4, 2, 0]; Stat: [0/6, 0/6, 0/6]\n";
  std::cout << ghost_cache;

  ghost_cache.access(6);
  ghost_cache.access(7);
  std::cout << "Expect: Boundaries: [6, 4, 2]; Stat: [0/8, 0/8, 0/8]\n";
  std::cout << ghost_cache;

  ghost_cache.access(1);
  std::cout << "Expect: Boundaries: [7, 5, 3]; Stat: [0/9, 0/9, 0/9]\n";
  std::cout << ghost_cache;

  ghost_cache.access(4);
  std::cout << "Expect: Boundaries: [1, 6, 3]; Stat: [0/10, 0/10, 1/10]\n";
  std::cout << ghost_cache;
}

void bench1() {
  GhostCache ghost_cache(bench_size / 16, bench_size / 16, bench_size);

  // filling the cache
  auto ts0 = rdtsc();
  for (uint32_t i = 0; i < bench_size; ++i) ghost_cache.access(i);
  auto ts1 = rdtsc();

  // cache hit
  for (uint32_t i = 0; i < bench_size; ++i) ghost_cache.access(i);
  auto ts2 = rdtsc();

  // cache miss
  for (uint32_t i = 0; i < bench_size; ++i) ghost_cache.access(i + bench_size);
  auto ts3 = rdtsc();

  for (uint32_t i = 0; i < bench_size / 8; ++i) {
    // manually flatten the loop, becaues we cannot turn on compiler
    // optimization which will remove the whole loop since it produces nothing.
    gcache_hash(i * 8);
    gcache_hash(i * 8 + 1);
    gcache_hash(i * 8 + 2);
    gcache_hash(i * 8 + 3);
    gcache_hash(i * 8 + 4);
    gcache_hash(i * 8 + 5);
    gcache_hash(i * 8 + 6);
    gcache_hash(i * 8 + 7);
  }
  auto ts4 = rdtsc();

  std::cout << "Fill: " << (ts1 - ts0) / bench_size << " cycles/op\n";
  std::cout << "Hit:  " << (ts2 - ts1) / bench_size << " cycles/op\n";
  std::cout << "Miss: " << (ts3 - ts2) / bench_size << " cycles/op\n";
  std::cout << "Hash: " << (ts4 - ts3) / bench_size << " cycles/op\n";
}

void bench2() {
  SampleGhostCache<5> sample_ghost_cache(bench_size / 16, bench_size / 16,
                                         bench_size);

  // filling the cache
  auto ts0 = rdtsc();
  for (uint32_t i = 0; i < bench_size; ++i) sample_ghost_cache.access(i);
  auto ts1 = rdtsc();

  // cache hit
  for (uint32_t i = 0; i < bench_size; ++i) sample_ghost_cache.access(i);
  auto ts2 = rdtsc();

  // cache miss
  for (uint32_t i = 0; i < bench_size; ++i)
    sample_ghost_cache.access(i + bench_size);
  auto ts3 = rdtsc();

  std::cout << "Fill: " << (ts1 - ts0) / bench_size << " cycles/op\n";
  std::cout << "Hit:  " << (ts2 - ts1) / bench_size << " cycles/op\n";
  std::cout << "Miss: " << (ts3 - ts2) / bench_size << " cycles/op\n";
}

void bench3() {
  GhostCache ghost_cache(bench_size / 16, bench_size / 16, bench_size);
  SampleGhostCache<5> sample_ghost_cache(bench_size / 16, bench_size / 16,
                                         bench_size);

  // filling the cache
  std::vector<uint32_t> reqs;
  for (uint32_t i = 0; i < bench_size; ++i) {
    ghost_cache.access(i);
    sample_ghost_cache.access(i);
    reqs.emplace_back(i);
  }

  std::shuffle(reqs.begin(), reqs.end(), std::default_random_engine());

  // cache hit
  auto ts0 = rdtsc();
  for (auto i : reqs) ghost_cache.access(i);
  auto ts1 = rdtsc();
  for (auto i : reqs) sample_ghost_cache.access(i);
  auto ts2 = rdtsc();

  std::cout << "w/o sampling: " << (ts1 - ts0) / bench_size << " cycles/op\n";
  std::cout << "w/ sampling:  " << (ts2 - ts1) / bench_size << " cycles/op\n";
  std::cout << "=============== Hit Rate ===============\n";
  std::cout << std::setw(8) << "size" << std::setw(16) << "w/o sampling"
            << std::setw(16) << "w/ sampling" << '\n';
  std::cout << "----------------------------------------\n";
  for (uint32_t s = bench_size / 16; s < bench_size; s += bench_size / 16) {
    std::cout << std::setw(7) << s / 1024 << 'K';
    std::cout << std::setw(15) << std::fixed << std::setprecision(3)
              << ghost_cache.get_hit_rate(s) * 100 << '%';
    std::cout << std::setw(15) << std::fixed << std::setprecision(3)
              << sample_ghost_cache.get_hit_rate(s) * 100 << "%\n";
  }
  std::cout << "========================================\n";
}

void bench4() {
  GhostCache ghost_cache(large_bench_size / 16, large_bench_size / 16,
                         large_bench_size);
  SampleGhostCache<5> sample_ghost_cache(
      large_bench_size / 16, large_bench_size / 16, large_bench_size);

  // filling the cache
  std::vector<uint32_t> reqs;
  for (uint32_t i = 0; i < large_bench_size; ++i) {
    ghost_cache.access(i);
    sample_ghost_cache.access(i);
    reqs.emplace_back(i);
  }

  std::shuffle(reqs.begin(), reqs.end(), std::default_random_engine());

  // cache hit
  auto ts0 = rdtsc();
  for (auto i : reqs) ghost_cache.access(i);
  auto ts1 = rdtsc();
  for (auto i : reqs) sample_ghost_cache.access(i);
  auto ts2 = rdtsc();

  std::cout << "w/o sampling: " << (ts1 - ts0) / large_bench_size
            << " cycles/op\n";
  std::cout << "w/ sampling:  " << (ts2 - ts1) / large_bench_size
            << " cycles/op\n";
  std::cout << "=============== Hit Rate ===============\n";
  std::cout << std::setw(8) << "size" << std::setw(16) << "w/o sampling"
            << std::setw(16) << "w/ sampling" << '\n';
  std::cout << "----------------------------------------\n";
  for (uint32_t s = large_bench_size / 16; s < large_bench_size;
       s += large_bench_size / 16) {
    std::cout << std::setw(7) << s / 1024 << 'K';
    std::cout << std::setw(15) << std::fixed << std::setprecision(3)
              << ghost_cache.get_hit_rate(s) * 100 << '%';
    std::cout << std::setw(15) << std::fixed << std::setprecision(3)
              << sample_ghost_cache.get_hit_rate(s) * 100 << "%\n";
  }
  std::cout << "========================================\n";
}

int main() {
  // test1();
  // test2();
  bench1();  // ghost cache w/o sampling
  bench2();  // ghost cache w/ sampling
  bench3();  // hit rate comparsion
  bench4();  // large bench: may exceed CPU cache size
  return 0;
}
