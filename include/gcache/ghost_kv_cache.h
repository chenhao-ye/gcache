#pragma once
#include <cstdint>
#include <string_view>

#include "ghost_cache.h"

namespace gcache {

struct GhostKvMeta {
  uint32_t size_idx;
  uint32_t kv_size;
};

/**
 * Simulate a key-value cache. It differs from GhostCache in that the key-value
 * pair can be variable-length. By default support sampling (non-sampling
 * version can be acquired by setting SampleShift=0)
 */
template <uint32_t SampleShift = 5, typename Hash = std::hash<std::string_view>>
class SampledGhostKvCache {
  SampledGhostCache<SampleShift, idhash, GhostKvMeta> ghost_cache;

 public:
  using Handle_t =
      typename SampledGhostCache<SampleShift, idhash, GhostKvMeta>::Handle_t;
  using Node_t =
      typename SampledGhostCache<SampleShift, idhash, GhostKvMeta>::Node_t;

 public:
  SampledGhostKvCache(uint32_t tick, uint32_t min_count, uint32_t max_count)
      : ghost_cache(tick, min_count, max_count) {
    static_assert(SampleShift <= 32, "SampleShift must be no larger than 32");
  }

  void access(const std::string_view key, uint32_t kv_size,
              AccessMode mode = AccessMode::DEFAULT) {
    uint32_t key_hash = Hash{}(key);
    access(key_hash, kv_size, mode);
  }

  void access(uint32_t key_hash, uint32_t kv_size,
              AccessMode mode = AccessMode::DEFAULT) {
    // only with certain number of leading zeros is sampled
    if constexpr (SampleShift > 0) {
      if (key_hash >> (32 - SampleShift)) return;
    }
    auto h = ghost_cache.access_impl(key_hash, key_hash, mode);
    h->kv_size = kv_size;
  }

  // for compatibility with GhostCache: APIs to query by keys count
  [[nodiscard]] uint32_t get_tick() const { return ghost_cache.get_tick(); }
  [[nodiscard]] uint32_t get_min_count() const {
    return ghost_cache.get_min_size();
  }
  [[nodiscard]] uint32_t get_max_count() const {
    return ghost_cache.get_max_size();
  }
  [[nodiscard]] double get_hit_rate(uint32_t count) {
    return ghost_cache.get_hit_rate(count);
  }
  [[nodiscard]] double get_miss_rate(uint32_t count) {
    return ghost_cache.get_miss_rate(count);
  }
  [[nodiscard]] const CacheStat& get_stat(uint32_t count) {
    return ghost_cache.get_stat(count);
  }

  void reset_stat() { ghost_cache.reset_stat(); }

  // For each item in the LRU list, call fn in LRU order
  template <typename Fn>
  void for_each_lru(Fn&& fn) const {
    ghost_cache.for_each_lru(fn);
  }

  // For each item in the LRU list, call fn in MRU order
  template <typename Fn>
  void for_each_mru(Fn&& fn) const {
    ghost_cache.for_each_mru(fn);
  }

  // For each item in the LRU list, call fn in LRU order until false
  template <typename Fn>
  void for_each_until_lru(Fn&& fn) const {
    ghost_cache.for_each_until_lru(fn);
  }

  // For each item in the LRU list, call fn in MRU order until false
  template <typename Fn>
  void for_each_until_mru(Fn&& fn) const {
    ghost_cache.for_each_until_mru(fn);
  }

  [[nodiscard]] const std::vector<std::tuple<
      /*count*/ uint32_t, /*size*/ uint32_t, /*miss_rate*/ CacheStat>>
  get_cache_stat_curve() {
    std::vector<std::tuple<uint32_t, uint32_t, CacheStat>> curve;
    uint32_t curr_count = 0;
    uint32_t curr_size = 0;
    ghost_cache.unsafe_for_each_mru([&](Handle_t h) {
      curr_size += h->kv_size;
      ++curr_count;
      if (curr_count >= ghost_cache.min_size &&
          (curr_count - ghost_cache.min_size) % ghost_cache.tick == 0) {
        curve.emplace_back(curr_count << SampleShift, curr_size << SampleShift,
                           ghost_cache.get_stat_shifted(curr_count));
      }
    });
    return curve;
    // should be implicitly moved by compiler
    // avoid explict move for Return Value Optimization (RVO)
  }
};
}  // namespace gcache
