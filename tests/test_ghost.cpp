#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>

#include "gcache/ghost_cache.h"
#include "gcache/node.h"
#include "util.h"

using namespace gcache;
constexpr const uint32_t num_ops = 32 * 1024 * 1024;
constexpr const uint32_t bench_size = 256 * 1024;             // 1 GB cache
constexpr const uint32_t large_bench_size = 2 * 1024 * 1024;  // 8 GB cache

constexpr const uint32_t sample_shift = 5;

void test1() {
  std::cout << "=== Test 1 ===\n";
  GhostCache<> ghost_cache(1, 3, 6);

  ghost_cache.access(0);
  ghost_cache.access(1);
  ghost_cache.access(2);
  ghost_cache.access(3);
  std::cout << "Ops: Access [0, 1, 2, 3]" << std::endl;
  std::cout << "Expect: Boundaries: [1, 0, (null)]; "
               "Stat: [0/4, 0/4, 0/4, 0/4]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(4);
  ghost_cache.access(5);
  std::cout << "Ops: Access [4, 5]" << std::endl;
  std::cout << "Expect: Boundaries: [3, 2, 1]; Stat: [0/6, 0/6, 0/6, 0/6]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(2);
  std::cout << "Ops: Access [2]" << std::endl;
  std::cout << "Expect: Boundaries: [4, 3, 1]; Stat: [0/7, 1/7, 1/7, 1/7]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(4);
  std::cout << "Ops: Access [4]" << std::endl;
  std::cout << "Expect: Boundaries: [5, 3, 1]; Stat: [1/8, 2/8, 2/8, 2/8]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(2, AccessMode::AS_MISS);
  std::cout << "Ops: Access [2:AS_MISS]" << std::endl;
  std::cout << "Expect: Boundaries: [5, 3, 1]; Stat: [1/9, 2/9, 2/9, 2/9]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(0, AccessMode::AS_HIT);
  std::cout << "Ops: Access [0:AS_HIT]" << std::endl;
  std::cout
      << "Expect: Boundaries: [4, 5, 3]; Stat: [2/10, 3/10, 3/10, 3/10]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(7, AccessMode::NOOP);
  std::cout << "Ops: Access [7:NOOP]" << std::endl;
  std::cout
      << "Expect: Boundaries: [2, 4, 5]; Stat: [2/10, 3/10, 3/10, 3/10]\n";
  std::cout << ghost_cache << std::endl;
}

void test2() {
  std::cout << "=== Test 2 ===\n";
  GhostCache<> ghost_cache(2, 2, 6);

  ghost_cache.access(0);
  ghost_cache.access(1);
  ghost_cache.access(2);
  ghost_cache.access(3);
  std::cout << "Ops: Access [0, 1, 2, 3]" << std::endl;
  std::cout << "Expect: Boundaries: [2, 0]; Stat: [0/4, 0/4, 0/4]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(4);
  ghost_cache.access(5);
  std::cout << "Ops: Access [4, 5]" << std::endl;
  std::cout << "Expect: Boundaries: [4, 2]; Stat: [0/6, 0/6, 0/6]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(6);
  ghost_cache.access(7);
  std::cout << "Ops: Access [6, 7]" << std::endl;
  std::cout << "Expect: Boundaries: [6, 4]; Stat: [0/8, 0/8, 0/8]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(1);
  std::cout << "Ops: Access [1]" << std::endl;
  std::cout << "Expect: Boundaries: [7, 5]; Stat: [0/9, 0/9, 0/9]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(4);
  std::cout << "Ops: Access [4]" << std::endl;
  std::cout << "Expect: Boundaries: [1, 6]; Stat: [0/10, 0/10, 1/10]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(8, AccessMode::NOOP);
  std::cout << "Ops: Access [8:NOOP]" << std::endl;
  std::cout << "Expect: Boundaries: [4, 7]; Stat: [0/10, 0/10, 1/10]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(9, AccessMode::AS_HIT);
  std::cout << "Ops: Access [9:AS_HIT]" << std::endl;
  std::cout << "Expect: Boundaries: [8, 1]; Stat: [1/11, 1/11, 2/11]\n";
  std::cout << ghost_cache << std::endl;

  ghost_cache.access(1, AccessMode::AS_MISS);
  std::cout << "Ops: Access [1:AS_MISS]" << std::endl;
  std::cout << "Expect: Boundaries: [9, 4]; Stat: [1/12, 1/12, 2/12]\n";
  std::cout << ghost_cache << std::endl;
}

// for checkpoint and recover
void test3() {
  GhostCache<> ghost_cache(2, 2, 6);
  std::cout << "=== Test 3 ===\n";

  ghost_cache.access(0);
  ghost_cache.access(1);
  ghost_cache.access(2);
  ghost_cache.access(3);
  ghost_cache.access(4);
  ghost_cache.access(5);
  ghost_cache.access(6);
  ghost_cache.access(7);
  ghost_cache.access(1);
  ghost_cache.access(4);
  ghost_cache.access(8);
  ghost_cache.access(9);
  ghost_cache.access(1);
  std::cout << "Ops: Access [0, 1, 2, 3, 4, 5, 6, 7, 1, 4, 8, 9, 1]"
            << std::endl;

  std::vector<uint32_t> ckpt;
  ghost_cache.for_each_lru([&ckpt](uint32_t key) { ckpt.emplace_back(key); });

  GhostCache<> ghost_cache2(3, 2, 11);
  for (auto key : ckpt) ghost_cache2.access(key, AccessMode::NOOP);

  std::cout << "Recover from checkpoint" << std::endl;
  std::cout
      << "Expect: LRU: [6, 7, 4, 8, 9, 1]; Boundaries: [9, 7, (null), (null)]; "
         "Stat: [0/0, 0/0, 0/0, 0/0]\n";
  std::cout << ghost_cache2;

  std::cout << "Ops: Access [2, 4, 3, 0]" << std::endl;
  ghost_cache2.access(2);
  ghost_cache2.access(4);
  ghost_cache2.access(3);
  ghost_cache2.access(0);
  std::cout << "Expect: LRU: [6, 7, 8, 9, 1, 2, 4, 3, 0]; Boundaries: [3, 1, "
               "7, (null)]; "
               "Stat: [0/4, 1/4, 1/4, 1/4]\n";
  std::cout << ghost_cache2;

  std::cout << std::endl;
}

void bench1() {
  GhostCache<> ghost_cache(bench_size / 32, bench_size / 32, bench_size);

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
    gcache::ghash{}(i * 8);
    gcache::ghash{}(i * 8 + 1);
    gcache::ghash{}(i * 8 + 2);
    gcache::ghash{}(i * 8 + 3);
    gcache::ghash{}(i * 8 + 4);
    gcache::ghash{}(i * 8 + 5);
    gcache::ghash{}(i * 8 + 6);
    gcache::ghash{}(i * 8 + 7);
  }
  auto ts4 = rdtsc();

  std::cout << "=== Bench 1 ===\n";
  std::cout << "Fill: " << (ts1 - ts0) / bench_size << " cycles/op\n";
  std::cout << "Hit:  " << (ts2 - ts1) / bench_size << " cycles/op\n";
  std::cout << "Miss: " << (ts3 - ts2) / bench_size << " cycles/op\n";
  std::cout << "Hash: " << (ts4 - ts3) / bench_size << " cycles/op\n";
  std::cout << std::endl;
}

void bench2() {
  SampledGhostCache<sample_shift> sampled_ghost_cache(
      bench_size / 32, bench_size / 32, bench_size);

  // filling the cache
  auto ts0 = rdtsc();
  for (uint32_t i = 0; i < bench_size; ++i) sampled_ghost_cache.access(i);
  auto ts1 = rdtsc();

  // cache hit
  for (uint32_t i = 0; i < bench_size; ++i) sampled_ghost_cache.access(i);
  auto ts2 = rdtsc();

  // cache miss
  for (uint32_t i = 0; i < bench_size; ++i)
    sampled_ghost_cache.access(i + bench_size);
  auto ts3 = rdtsc();

  std::cout << "=== Bench 2 ===\n";
  std::cout << "Fill: " << (ts1 - ts0) / bench_size << " cycles/op\n";
  std::cout << "Hit:  " << (ts2 - ts1) / bench_size << " cycles/op\n";
  std::cout << "Miss: " << (ts3 - ts2) / bench_size << " cycles/op\n";
  std::cout << std::endl;
}

void bench3() {
  GhostCache<> ghost_cache(bench_size / 32, bench_size / 32, bench_size);
  SampledGhostCache<sample_shift> sampled_ghost_cache(
      bench_size / 32, bench_size / 32, bench_size);

  // filling the cache
  std::vector<uint32_t> reqs;
  // working set is 1.2x of max cache size
  for (uint32_t i = 0; i < bench_size; ++i) {
    ghost_cache.access(i);
    sampled_ghost_cache.access(i);
    reqs.emplace_back(i);
  }
  ghost_cache.reset_stat();
  sampled_ghost_cache.reset_stat();
  std::random_shuffle(reqs.begin(), reqs.end());

  uint64_t elapse_g = 0;
  uint64_t elapse_s = 0;
  uint64_t ts0 = rdtsc();
  for (uint32_t i = 0; i < num_ops / reqs.size(); ++i) {
    for (auto j : reqs) ghost_cache.access(j);
    elapse_g += rdtsc() - ts0;
    std::random_shuffle(reqs.begin(), reqs.end());
    ts0 = rdtsc();
  }

  ts0 = rdtsc();
  for (uint32_t i = 0; i < num_ops / reqs.size(); ++i) {
    for (auto j : reqs) sampled_ghost_cache.access(j);
    elapse_s += rdtsc() - ts0;
    std::random_shuffle(reqs.begin(), reqs.end());
    ts0 = rdtsc();
  }

  std::cout << "=== Bench 3 ===\n";
  std::cout << "w/o sampling: " << elapse_g / num_ops << " cycles/op\n";
  std::cout << "w/ sampling:  " << elapse_s / num_ops << " cycles/op\n";
  std::cout
      << "=========================== Hit Rate ===========================\n"
      << "  size          w/o sampling                 w/ sampling        \n";
  std::cout
      << "----------------------------------------------------------------\n";
  for (uint32_t s = bench_size / 32; s <= bench_size; s += bench_size / 32) {
    std::cout << std::setw(7) << s / 1024 << "K ";
    ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << ' ';
    sampled_ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << '\n';
  }
  std::cout
      << "================================================================\n";
  std::cout << std::endl;
}

void bench4() {
  GhostCache<> ghost_cache(large_bench_size / 32, large_bench_size / 32,
                           large_bench_size);
  SampledGhostCache<sample_shift> sampled_ghost_cache(
      large_bench_size / 32, large_bench_size / 32, large_bench_size);

  // filling the cache
  std::vector<uint32_t> reqs;
  for (uint32_t i = 0; i < large_bench_size; ++i) {
    ghost_cache.access(i);
    sampled_ghost_cache.access(i);
    reqs.emplace_back(i);
  }
  ghost_cache.reset_stat();
  sampled_ghost_cache.reset_stat();
  std::random_shuffle(reqs.begin(), reqs.end());

  uint64_t elapse_g = 0;
  uint64_t elapse_s = 0;
  uint64_t ts0 = rdtsc();
  for (uint32_t i = 0; i < num_ops / reqs.size(); ++i) {
    for (auto j : reqs) ghost_cache.access(j);
    elapse_g += rdtsc() - ts0;
    std::random_shuffle(reqs.begin(), reqs.end());
    ts0 = rdtsc();
  }

  ts0 = rdtsc();
  for (uint32_t i = 0; i < num_ops / reqs.size(); ++i) {
    for (auto j : reqs) sampled_ghost_cache.access(j);
    elapse_s += rdtsc() - ts0;
    std::random_shuffle(reqs.begin(), reqs.end());
    ts0 = rdtsc();
  }

  std::cout << "=== Bench 4 ===\n";
  std::cout << "w/o sampling: " << elapse_g / num_ops << " cycles/op\n";
  std::cout << "w/ sampling:  " << elapse_s / num_ops << " cycles/op\n";
  std::cout
      << "=========================== Hit Rate ===========================\n"
      << "  size          w/o sampling                 w/ sampling        \n";
  std::cout
      << "----------------------------------------------------------------\n";
  for (uint32_t s = large_bench_size / 32; s <= large_bench_size;
       s += large_bench_size / 32) {
    std::cout << std::setw(7) << s / 1024 << "K ";
    ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << ' ';
    sampled_ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << '\n';
  }
  std::cout
      << "================================================================\n";
  std::cout << std::endl;
}

void bench5() {
  GhostCache<> ghost_cache(large_bench_size / 32, large_bench_size / 32,
                           large_bench_size);
  SampledGhostCache<sample_shift> sampled_ghost_cache(
      large_bench_size / 32, large_bench_size / 32, large_bench_size);

  // filling the cache
  std::vector<uint32_t> reqs;
  for (uint32_t i = 0; i < large_bench_size; ++i) {
    ghost_cache.access(i);
    sampled_ghost_cache.access(i);
  }
  for (uint32_t i = 0; i < num_ops; ++i)
    reqs.emplace_back(rand() % large_bench_size);
  ghost_cache.reset_stat();
  sampled_ghost_cache.reset_stat();
  std::random_shuffle(reqs.begin(), reqs.end());

  uint64_t ts0;
  ts0 = rdtsc();
  for (auto i : reqs) ghost_cache.access(i);
  uint64_t elapse_g = rdtsc() - ts0;

  ts0 = rdtsc();
  for (auto i : reqs) sampled_ghost_cache.access(i);
  uint64_t elapse_s = rdtsc() - ts0;

  std::cout << "=== Bench 5 ===\n";
  std::cout << "w/o sampling: " << elapse_g / num_ops << " cycles/op\n";
  std::cout << "w/ sampling:  " << elapse_s / num_ops << " cycles/op\n";
  std::cout
      << "=========================== Hit Rate ===========================\n"
      << "  size          w/o sampling                 w/ sampling        \n";
  std::cout
      << "----------------------------------------------------------------\n";
  for (uint32_t s = large_bench_size / 32; s <= large_bench_size;
       s += large_bench_size / 32) {
    std::cout << std::setw(7) << s / 1024 << "K ";
    ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << ' ';
    sampled_ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << '\n';
  }
  std::cout
      << "================================================================\n";
  std::cout << std::endl;
}

int main() {
  test1();
  test2();
  test3();   // test checkpoint and recover
  bench1();  // ghost cache w/o sampling
  bench2();  // ghost cache w/ sampling
  bench3();  // hit rate comparsion
  bench4();  // large bench: may exceed CPU cache size
  bench5();  // real random access
  return 0;
}
