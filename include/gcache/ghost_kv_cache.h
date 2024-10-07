#pragma once
#include <cassert>
#include <cstdint>

#include "ghost_cache.h"

namespace gcache {

struct GhostKvMeta {
  uint32_t size_idx;
  uint32_t kv_size;
};

/**
 * Simulate a key-value cache. It differs from GhostCache in that the key-value
 * pair can be variable-length.
 */
template <typename HashStr = ghash_str>
class GhostKvCache {
  GhostCache<idhash, GhostKvMeta> ghost_cache;

 public:
  using Handle_t = typename GhostCache<idhash, GhostKvMeta>::Handle_t;
  using Node_t = typename GhostCache<idhash, GhostKvMeta>::Node_t;

 public:
  GhostKvCache(uint32_t tick, uint32_t min_count, uint32_t max_count)
      : ghost_cache(tick, min_count, max_count) {}

  void access(const std::string& key, uint32_t kv_size,
              AccessMode mode = AccessMode::DEFAULT) {
    access(key.data(), key.size(), kv_size, mode);
  }

  void access(const char* key, size_t key_len, uint32_t kv_size,
              AccessMode mode = AccessMode::DEFAULT) {
    uint32_t key_hash = HashStr{}(key, key_len);
    access(key_hash, kv_size, mode);
  }

  void access(uint32_t key_hash, uint32_t kv_size,
              AccessMode mode = AccessMode::DEFAULT) {
    // only with certain number of leading zeros is sampled
    if (key_hash >> (32 - this->SampleShift)) return;
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
  [[nodiscard]] double get_hit_rate(uint32_t count) const {
    return ghost_cache.get_hit_rate(count);
  }
  [[nodiscard]] double get_miss_rate(uint32_t count) const {
    return ghost_cache.get_miss_rate(count);
  }
  [[nodiscard]] const CacheStat& get_stat(uint32_t count) const {
    return ghost_cache.get_stat(count);
  }

  [[nodiscard]] const std::vector<
      std::tuple</*count*/ uint32_t, /*size*/ uint32_t, /*miss_rate*/ double>>
  get_miss_rate_curve() const {
    uint32_t tick = get_tick();
    uint32_t min_count = get_min_count();
    std::vector<std::tuple<uint32_t, uint32_t, double>> mrc;
    uint32_t curr_count = 0;
    uint32_t curr_size = 0;
    ghost_cache.unsafe_for_each_mru([&](Handle_t h) {
      curr_size += h->kv_size;
      ++curr_count;
      if ((curr_count - min_count) % tick == 0)
        mrc.emplace_back(curr_count, curr_size, get_miss_rate(curr_count));
    });
    return mrc;
    // should be implictly moved by compiler
    // avoid explict move for Return Value Optimization (RVO)
  }
};
}  // namespace gcache
