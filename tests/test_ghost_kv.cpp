#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

#include "gcache/ghost_cache.h"
#include "gcache/ghost_kv_cache.h"
#include "gcache/node.h"
#include "util.h"

using namespace gcache;
constexpr const uint32_t num_ops = 1 * 1024 * 1024;
constexpr const uint32_t bench_size = 1 * 1024 * 1024;  // 1m keys
constexpr const uint32_t sample_shift = 5;

std::random_device rd;    // non-deterministic random device
std::mt19937 urbg(rd());  // UniformRandomBitGenerator

std::string make_key(int k) {
  std::ostringstream stream;
  stream << std::setw(16) << std::setfill('0') << k;
  return stream.str();
}

void bench1() {
  uint32_t tick = bench_size / 64;
  GhostCache<> ghost_cache(tick, tick, bench_size);
  SampledGhostCache<sample_shift> sampled_ghost_cache(tick, tick, bench_size);
  SampledGhostKvCache<sample_shift> sampled_ghost_kv_cache(tick, tick,
                                                           bench_size);

  // filling the cache
  for (uint32_t i = 0; i < bench_size; ++i) {
    ghost_cache.access(i, AccessMode::NOOP);
    auto k = make_key(i);
    sampled_ghost_cache.access(std::hash<std::string_view>{}(k),
                               AccessMode::NOOP);
    sampled_ghost_kv_cache.access(k, i > bench_size / 4 ? 500 : 2000,
                                  AccessMode::NOOP);
  }

  std::vector<uint32_t> reqs;
  std::vector<std::pair<uint32_t, std::string>> reqs2;
  for (uint32_t i = 0; i < num_ops; ++i) reqs.emplace_back(rand() % bench_size);
  std::shuffle(reqs.begin(), reqs.end(), urbg);
  for (auto i : reqs) reqs2.emplace_back(i, make_key(i));

  uint64_t t0 = rdtsc();
  for (auto i : reqs) ghost_cache.access(i);
  uint64_t elapsed_ghost = rdtsc() - t0;

  t0 = rdtsc();
  for (const auto& [i, k] : reqs2)
    sampled_ghost_cache.access(std::hash<std::string_view>{}(k));
  uint64_t elapsed_sampled = rdtsc() - t0;

  t0 = rdtsc();
  for (const auto& [i, k] : reqs2)
    sampled_ghost_kv_cache.access(k, i > bench_size / 4 ? 500 : 2000);
  uint64_t elapsed_sampled_kv = rdtsc() - t0;

  std::cout << "=== Bench 1 ===\n"
            << "w/o sampling: " << elapsed_ghost / num_ops << " cycles/op\n"
            << "w/ sampling:  " << elapsed_sampled / num_ops << " cycles/op\n"
            << "w/ kv sampling:  " << elapsed_sampled_kv / num_ops
            << " cycles/op\n"
            << "================================================= Hit Rate "
               "===================================================\n"
               " size |       w/o sampling       |        w/ sampling       "
               "|       w/ kv sampling     |        kv memoy       \n"
               "-------------------------------------------------------------"
               "-------------------------------------------------\n";

  auto curve = sampled_ghost_kv_cache.get_cache_stat_curve();
  for (uint32_t s = tick; s <= bench_size; s += tick) {
    std::cout << std::setw(5) << s / 1024 << "K|";
    ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << '|';
    sampled_ghost_cache.get_stat(s).print(std::cout, 8);
    std::cout << '|';
    sampled_ghost_kv_cache.get_stat(s).print(std::cout, 8);
    std::cout << '|';
    auto idx = s / tick - 1;
    if (idx < curve.size()) {
      auto [count, size, cache_stat] = curve[idx];
      assert(count == s);
      if (cache_stat.hit_cnt == 0) {
        std::cout << "  NAN";
      } else {
        std::cout << std::setw(5) << std::fixed << std::setprecision(1)
                  << cache_stat.get_hit_rate() * 100 << '%';
      }
      std::cout << " @" << std::setw(7) << std::fixed << size / 1024 / 1024
                << 'M' << std::setw(5) << std::fixed << size / count;
    }
    std::cout << std::endl;
  }
  std::cout << "=============================================================="
            << "================================================\n"
            << std::endl;
}

void test_update_size() {
  uint32_t tick = bench_size / 64;
  SampledGhostKvCache<sample_shift> sampled_ghost_kv_cache(tick, tick,
                                                           bench_size);
  SampledGhostKvCache<sample_shift> sampled_ghost_kv_cache2(tick, tick,
                                                            bench_size);

  // filling the cache
  for (uint32_t i = 0; i < bench_size; ++i) {
    auto k = make_key(i);
    sampled_ghost_kv_cache.access(k, i > bench_size / 4 ? 500 : 2000,
                                  AccessMode::NOOP);
    sampled_ghost_kv_cache2.access(k, i > bench_size / 4 ? 500 : 2000,
                                   AccessMode::NOOP);
  }

  std::vector<uint32_t> reqs;
  std::vector<std::pair<uint32_t, std::string>> reqs2;
  for (uint32_t i = 0; i < num_ops; ++i) reqs.emplace_back(rand() % bench_size);
  std::shuffle(reqs.begin(), reqs.end(), urbg);
  for (auto i : reqs) reqs2.emplace_back(i, make_key(i));

  for (const auto& [i, k] : reqs2) {
    sampled_ghost_kv_cache.access(k, i > bench_size / 4 ? 500 : 2000);
    sampled_ghost_kv_cache2.access(k, i > bench_size / 4 ? 500 : 2000);
  }
  std::shuffle(reqs2.begin(), reqs2.end(), urbg);
  for (const auto& [i, k] : reqs2)
    sampled_ghost_kv_cache2.update_size(k, (2000 * 1 + 500 * 3) / 4);

  // Dump keys using for_each_lru and compare
  std::vector<std::pair<uint32_t, uint32_t>> keys1, keys2;
  sampled_ghost_kv_cache.for_each_lru(
      [&keys1](const auto& h) { keys1.emplace_back(h.get_key(), h->kv_size); });
  sampled_ghost_kv_cache2.for_each_lru(
      [&keys2](const auto& h) { keys2.emplace_back(h.get_key(), h->kv_size); });

  // Compare vectors
  if (keys1.size() != keys2.size())
    throw std::runtime_error("Key count mismatch");
  for (size_t i = 0; i < keys1.size(); ++i) {
    if (keys1[i].first != keys2[i].first) {
      throw std::runtime_error(
          "Key mismatch: lhs=" + std::to_string(keys1[i].first) +
          ", rhs=" + std::to_string(keys2[i].first));
    }
  }

  // Print comparison table
  auto curve1 = sampled_ghost_kv_cache.get_cache_stat_curve();
  auto curve2 = sampled_ghost_kv_cache2.get_cache_stat_curve();

  std::cout << "=== Update Size Test ===\n"
            << "==============================================================="
               "==========================================\n"
            << " size |       w/ kv sampling     |       kv memory     "
               "|      w/ kv sample update |   updated kv memory  \n"
            << "---------------------------------------------------------------"
               "------------------------------------------\n";

  for (uint32_t s = tick; s <= bench_size; s += tick) {
    std::cout << std::setw(5) << s / 1024 << "K|";
    sampled_ghost_kv_cache.get_stat(s).print(std::cout, 8);
    std::cout << '|';

    auto idx = s / tick - 1;
    if (idx < curve1.size()) {
      auto [count, size, cache_stat] = curve1[idx];
      assert(count == s);
      if (cache_stat.hit_cnt == 0) {
        std::cout << "  NAN";
      } else {
        std::cout << std::setw(5) << std::fixed << std::setprecision(1)
                  << cache_stat.get_hit_rate() * 100 << '%';
      }
      std::cout << " @" << std::setw(7) << std::fixed << size / 1024 / 1024
                << 'M' << std::setw(5) << std::fixed << size / count;
    }
    std::cout << '|';

    sampled_ghost_kv_cache2.get_stat(s).print(std::cout, 8);
    std::cout << '|';

    if (idx < curve2.size()) {
      auto [count, size, cache_stat] = curve2[idx];
      assert(count == s);
      if (cache_stat.hit_cnt == 0) {
        std::cout << "  NAN";
      } else {
        std::cout << std::setw(5) << std::fixed << std::setprecision(1)
                  << cache_stat.get_hit_rate() * 100 << '%';
      }
      std::cout << " @" << std::setw(7) << std::fixed << size / 1024 / 1024
                << 'M' << std::setw(5) << std::fixed << size / count;
    }
    std::cout << std::endl;
  }
  std::cout << "==============================================================="
               "==========================================\n"
            << std::endl;
}

int main() {
  bench1();
  test_update_size();
}
