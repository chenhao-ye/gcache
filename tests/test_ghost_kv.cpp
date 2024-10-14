#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

#include "gcache/ghost_kv_cache.h"
#include "gcache/node.h"
#include "util.h"

using namespace gcache;
constexpr const uint32_t num_ops = 1 * 1024 * 1024;
constexpr const uint32_t bench_size = 1 * 1024 * 1024;  // 1m keys
constexpr const uint32_t sample_shift = 5;

std::string make_key(int k) {
  std::ostringstream stream;
  stream << std::setw(16) << std::setfill('0') << k;
  return stream.str();
}

void bench1() {
  uint32_t tick = bench_size / 64;
  GhostCache<> ghost_cache(tick, tick, bench_size);
  SampledGhostKvCache<sample_shift> sampled_ghost_kv_cache(tick, tick,
                                                           bench_size);

  // filling the cache
  std::vector<uint32_t> reqs;
  std::vector<std::pair<uint32_t, std::string>> reqs2;
  for (uint32_t i = 0; i < bench_size; ++i) {
    ghost_cache.access(i);
    sampled_ghost_kv_cache.access(make_key(i), i > bench_size / 4 ? 500 : 2000);
  }
  ghost_cache.reset_stat();
  sampled_ghost_kv_cache.reset_stat();

  for (uint32_t i = 0; i < num_ops; ++i) reqs.emplace_back(rand() % bench_size);
  std::random_shuffle(reqs.begin(), reqs.end());
  for (auto i : reqs) reqs2.emplace_back(i, make_key(i));

  uint64_t ts0;
  ts0 = rdtsc();
  for (auto i : reqs) ghost_cache.access(i);
  uint64_t elapse_g = rdtsc() - ts0;

  ts0 = rdtsc();
  for (const auto& [i, k] : reqs2)
    sampled_ghost_kv_cache.access(k, i > bench_size / 4 ? 500 : 2000);
  uint64_t elapse_s = rdtsc() - ts0;

  std::cout << "=== Bench 1 ===\n";
  std::cout << "w/o sampling: " << elapse_g / num_ops << " cycles/op\n";
  std::cout << "w/ sampling:  " << elapse_s / num_ops << " cycles/op\n";
  std::cout << "========================= Hit Rate ==========================="
            << "======================\n"
            << " size |       w/o sampling       |        w/ sampling       |"
            << "        kv memoy       \n"
            << "-------------------------------------------------------------"
            << "-----------------------\n";

  auto mrc = sampled_ghost_kv_cache.get_miss_rate_curve();
  for (uint32_t s = tick; s <= bench_size; s += tick) {
    std::cout << std::setw(5) << s / 1024 << "K|";
    ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << '|';
    sampled_ghost_kv_cache.get_stat(s).print(std::cout, 8);
    std::cout << '|';
    auto [count, size, miss_rate] = mrc[s / tick - 1];
    assert(count == s);
    if (miss_rate == std::numeric_limits<double>::infinity()) {
      std::cout << "  NAN";
    } else {
      std::cout << std::setw(5) << std::fixed << std::setprecision(1)
                << (1 - miss_rate) * 100 << '%';
    }
    std::cout << " @" << std::setw(7) << std::fixed << size / 1024 / 1024 << 'M'
              << std::setw(5) << std::fixed << size / count << std::endl;
  }
  std::cout << "=============================================================="
            << "======================\n";
  std::cout << std::endl;
}
int main() { bench1(); }
