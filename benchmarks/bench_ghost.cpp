#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>

#include "gcache/ghost_cache.h"
#include "gcache/handle.h"
#include "util.h"

using namespace gcache;

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
  GhostCache ghost_cache(16 * 1024, 16 * 1024, 256 * 1024);

  // filling the cache
  auto ts0 = rdtsc();
  for (int i = 0; i < 256 * 1024; ++i) ghost_cache.access(i);
  auto ts1 = rdtsc();

  // cache hit
  for (int i = 0; i < 256 * 1024; ++i) ghost_cache.access(i);
  auto ts2 = rdtsc();

  // cache miss
  for (int i = 0; i < 256 * 1024; ++i) ghost_cache.access(i + 256 * 1024);
  auto ts3 = rdtsc();

  for (int i = 0; i < 256 * 1024; ++i) fast_hash(i);
  auto ts4 = rdtsc();

  std::cout << "Fill: " << (ts1 - ts0) / (256 * 1024) << " cycles/op\n";
  std::cout << "Hit:  " << (ts2 - ts1) / (256 * 1024) << " cycles/op\n";
  std::cout << "Miss: " << (ts3 - ts2) / (256 * 1024) << " cycles/op\n";
  std::cout << "Hash: " << (ts4 - ts3) / (256 * 1024) << " cycles/op\n";
}

void bench2() {
  SampleGhostCache<5> sample_ghost_cache(16 * 1024 / 32, 16 * 1024 / 32,
                                         256 * 1024 / 32);

  // filling the cache
  auto ts0 = rdtsc();
  for (int i = 0; i < 256 * 1024; ++i) sample_ghost_cache.access(i);
  auto ts1 = rdtsc();

  // cache hit
  for (int i = 0; i < 256 * 1024; ++i) sample_ghost_cache.access(i);
  auto ts2 = rdtsc();

  // cache miss
  for (int i = 0; i < 256 * 1024; ++i)
    sample_ghost_cache.access(i + 256 * 1024);
  auto ts3 = rdtsc();

  for (int i = 0; i < 256 * 1024; ++i) fast_hash(i);
  auto ts4 = rdtsc();

  std::cout << "Fill: " << (ts1 - ts0) / (256 * 1024) << " cycles/op\n";
  std::cout << "Hit:  " << (ts2 - ts1) / (256 * 1024) << " cycles/op\n";
  std::cout << "Miss: " << (ts3 - ts2) / (256 * 1024) << " cycles/op\n";
  std::cout << "Hash: " << (ts4 - ts3) / (256 * 1024) << " cycles/op\n";
}

// 80/20 workload: 20% of hot entries consist of 80% of requests.
// On average, each hot entry is 16x hotter than a cold one.
void bench3() {
  GhostCache ghost_cache(16 * 1024, 16 * 1024, 256 * 1024);
  SampleGhostCache<5> sample_ghost_cache(16 * 1024 / 32, 16 * 1024 / 32,
                                         256 * 1024 / 32);

  // filling the cache
  for (int i = 0; i < 256 * 1024; ++i) {
    ghost_cache.access(i);
    sample_ghost_cache.access(i);
  }

  std::vector<uint32_t> reqs;
  uint32_t chunk_size = 256 * 1024 / 17;
  for (uint32_t i = 0; i < chunk_size; ++i)
    for (int i = 0; i < 16; ++i) reqs.emplace_back(i);
  for (uint32_t i = chunk_size; i < chunk_size * 17; ++i) reqs.emplace_back(i);
  std::shuffle(reqs.begin(), reqs.end(), std::default_random_engine());

  // cache hit
  auto ts0 = rdtsc();
  for (auto i : reqs) ghost_cache.access(i);
  auto ts1 = rdtsc();
  for (auto i : reqs) sample_ghost_cache.access(i);
  auto ts2 = rdtsc();

  std::cout << "w/o sampling: " << (ts1 - ts0) / (256 * 1024 / 17 * 17)
            << " cycles/op\n";
  for (uint32_t s = 16 * 1024; s < 256 * 1024; s += 16 * 1024)
    std::cout << "size=" << s << ": " << ghost_cache.get_hit_rate(s) << '\n';

  std::cout << "w/ sampling:  " << (ts2 - ts1) / (256 * 1024 / 17 * 17)
            << " cycles/op\n";
  for (uint32_t s = 16 * 1024 / 32; s < 256 * 1024 / 32; s += 16 * 1024 / 32)
    std::cout << "size=" << s << ": " << sample_ghost_cache.get_hit_rate(s)
              << '\n';
}

int main() {
  test1();
  test2();
  bench1();  // ghost cache w/o sampling
  bench2();  // ghost cache w/ sampling
  bench3();
  return 0;
}
