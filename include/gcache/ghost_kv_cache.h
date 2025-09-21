#pragma once
#include <cstdint>
#include <string_view>

#include "ghost_cache.h"

namespace gcache {

template <typename SizeType = uint32_t>
struct GhostKvMeta {
  SizeType size_idx;
  SizeType kv_size;
};

/**
 * Simulate a key-value cache. It differs from GhostCache in that the key-value
 * pair can be variable-length. By default support sampling (non-sampling
 * version can be acquired by setting SampleShift=0)
 */
template <uint32_t SampleShift = 5, typename Hash = gshash,
          typename SizeType = uint32_t, typename HashType = uint32_t>
class SampledGhostKvCache {
  SampledGhostCache<SampleShift, idhash, GhostKvMeta<SizeType>, SizeType,
                    HashType>
      ghost_cache;

 public:
  using Handle_t =
      typename SampledGhostCache<SampleShift, idhash, GhostKvMeta<SizeType>,
                                 SizeType, HashType>::Handle_t;
  using Node_t =
      typename SampledGhostCache<SampleShift, idhash, GhostKvMeta<SizeType>,
                                 SizeType, HashType>::Node_t;

 public:
  SampledGhostKvCache(SizeType tick, SizeType min_count, SizeType max_count)
      : ghost_cache(tick, min_count, max_count) {
    static_assert(SampleShift <= std::numeric_limits<SizeType>::digits);
  }

  void access(const std::string_view key, SizeType kv_size,
              AccessMode mode = AccessMode::DEFAULT) {
    access(Hash{}(key), kv_size, mode);
  }

  void access(HashType key_hash, SizeType kv_size,
              AccessMode mode = AccessMode::DEFAULT) {
    if constexpr (SampleShift > 0) {
      // only sample keys with certain number of leading zeros in hash
      if (key_hash >> (std::numeric_limits<SizeType>::digits - SampleShift))
        return;
    }
    auto h = ghost_cache.access_impl(key_hash, key_hash, mode);
    h->kv_size = kv_size;
  }

  void update_size(const std::string_view key, SizeType kv_size) {
    update_size(Hash{}(key), kv_size);
  }

  void update_size(HashType key_hash, SizeType kv_size) {
    if constexpr (SampleShift > 0) {
      // only sample keys with certain number of leading zeros in hash
      if (key_hash >> (std::numeric_limits<SizeType>::digits - SampleShift))
        return;
    }
    auto h = ghost_cache.lookup_no_refresh(key_hash, key_hash);
    if (h) h->kv_size = kv_size;
  }

  // for compatibility with GhostCache: APIs to query by keys count
  [[nodiscard]] SizeType get_tick() const { return ghost_cache.get_tick(); }
  [[nodiscard]] SizeType get_min_count() const {
    return ghost_cache.get_min_size();
  }
  [[nodiscard]] SizeType get_max_count() const {
    return ghost_cache.get_max_size();
  }
  [[nodiscard]] double get_hit_rate(SizeType count) {
    return ghost_cache.get_hit_rate(count);
  }
  [[nodiscard]] double get_miss_rate(SizeType count) {
    return ghost_cache.get_miss_rate(count);
  }
  [[nodiscard]] const CacheStat& get_stat(SizeType count) {
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

  // Since the aggregated kv_size can easiler grow beyond 4 GB, we use size_t
  // instead of SizeType for that
  [[nodiscard]] const std::vector<std::tuple<
      /*count*/ SizeType, /*size*/ size_t, /*miss_rate*/ CacheStat>>
  get_cache_stat_curve() {
    std::vector<std::tuple<SizeType, size_t, CacheStat>> curve;
    SizeType curr_count = 0;
    size_t curr_size = 0;
    ghost_cache.for_each_mru([&](const Handle_t h) {
      ++curr_count;
      curr_size += h->kv_size;
      if (curr_count >= ghost_cache.min_size &&
          (curr_count - ghost_cache.min_size) % ghost_cache.tick == 0) {
        curve.emplace_back(curr_count << SampleShift, curr_size << SampleShift,
                           ghost_cache.get_stat_shifted(curr_count));
      }
    });
    // the last handle may not be at a tick, which can happen when the working
    // set is smaller than max_size; we need to manually add this tick
    if (curr_count < ghost_cache.min_size) {
      auto next_count = ghost_cache.min_size;
      curve.emplace_back(next_count << SampleShift, curr_size << SampleShift,
                         ghost_cache.get_stat_shifted(next_count));
    } else if ((curr_count - ghost_cache.min_size) % ghost_cache.tick != 0) {
      // round up to the next tick
      auto next_count = (curr_count + ghost_cache.tick - 1) / ghost_cache.tick *
                        ghost_cache.tick;
      curve.emplace_back(next_count << SampleShift, curr_size << SampleShift,
                         ghost_cache.get_stat_shifted(next_count));
    }
    return curve;
    // should be implicitly moved by compiler
    // avoid explicit move for Return Value Optimization (RVO)
  }
};
}  // namespace gcache
