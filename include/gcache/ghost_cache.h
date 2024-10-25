#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

#include "hash.h"
#include "lru_cache.h"
#include "node.h"
#include "stat.h"

namespace gcache {

// AccessMode controls the behavior to update cache stat
enum AccessMode : uint8_t {
  DEFAULT,  // update normally
  AS_MISS,  // consider it as a miss for all cache sizes
  AS_HIT,   // consider it as a hit for all cache sizes
  NOOP,     // do not update
};

struct GhostMeta {
  uint32_t size_idx;
};

template <typename Hash>
class GhostKvCache;

/**
 * Simulate a set of page cache, where each page only carry thin metadata
 * Templated type Meta must have a field size_idx; in almost all cases, this
 * field does not need to specified; it is only useful if there is some
 * additional per-page metadata to be carried.
 */
template <typename Hash = ghash, typename Meta = GhostMeta>
class GhostCache {
 protected:
  const uint32_t tick;
  const uint32_t min_size;
  const uint32_t max_size;
  const uint32_t num_ticks;

  // Key is block_id/block number
  // Value is "size_idx", which is the least non-negative number such that the
  // key will in cache if the cache size is (size_idx * tick) + min_size
  LRUCache<uint32_t, Meta, Hash> cache;

 public:
  using Handle_t = typename LRUCache<uint32_t, Meta, Hash>::Handle_t;
  using Node_t = typename LRUCache<uint32_t, Meta, Hash>::Node_t;

 protected:
  // these must be placed after num_ticks to ensure a correct ctor order
  std::vector<Node_t*> boundaries;
  std::vector<CacheStat> caches_stat;

  // the reused distances are formatted as a histogram
  std::vector<uint32_t> reuse_distances;  // converted to caches_stat lazily
  uint32_t reuse_count;                   // count all access to reuse_distances

  Handle_t access_impl(uint32_t block_id, uint32_t hash, AccessMode mode);

  template <uint32_t S, typename H>
  friend class SampledGhostKvCache;

  void build_caches_stat();

 public:
  GhostCache(uint32_t tick, uint32_t min_size, uint32_t max_size)
      : tick(tick),
        min_size(min_size),
        max_size(max_size),
        num_ticks((max_size - min_size) / tick + 1),
        cache(),
        boundaries(num_ticks - 1, nullptr),
        caches_stat(num_ticks),
        reuse_distances(num_ticks, 0),
        reuse_count(0) {
    assert(tick > 0);
    assert(min_size > 1);  // otherwise the first boundary will be LRU evicted
    assert(min_size + (num_ticks - 1) * tick == max_size);
    assert(num_ticks > 2);
    cache.init(max_size);
  }

  void access(uint32_t block_id, AccessMode mode = AccessMode::DEFAULT) {
    access_impl(block_id, Hash{}(block_id), mode);
  }

  [[nodiscard]] uint32_t get_tick() const { return tick; }
  [[nodiscard]] uint32_t get_min_size() const { return min_size; }
  [[nodiscard]] uint32_t get_max_size() const { return max_size; }

  [[nodiscard]] const CacheStat& get_stat(uint32_t cache_size) {
    assert(cache_size >= min_size);
    assert(cache_size <= max_size);
    assert((cache_size - min_size) % tick == 0);
    uint32_t size_idx = (cache_size - min_size) / tick;
    assert(size_idx < num_ticks);
    const CacheStat& stat = caches_stat[size_idx];
    if (stat.hit_cnt + stat.miss_cnt != reuse_count) build_caches_stat();
    assert(stat.hit_cnt + stat.miss_cnt == reuse_count);
    return stat;
  }
  [[nodiscard]] double get_hit_rate(uint32_t cache_size) {
    return get_stat(cache_size).get_hit_rate();
  }
  [[nodiscard]] double get_miss_rate(uint32_t cache_size) {
    return get_stat(cache_size).get_miss_rate();
  }

  void reset_stat() {
    reuse_count = 0;
    for (size_t i = 0; i < reuse_distances.size(); ++i) reuse_distances[i] = 0;
  }

  // For each item in the LRU list, call fn in LRU order
  template <typename Fn>
  void for_each_lru(Fn&& fn) const {
    cache.for_each_lru([&fn](Handle_t h) { fn(h.get_key()); });
  }

  // For each item in the LRU list, call fn in MRU order
  template <typename Fn>
  void for_each_mru(Fn&& fn) const {
    cache.for_each_mru([&fn](Handle_t h) { fn(h.get_key()); });
  }

  // For each item in the LRU list, call fn in LRU order until false
  template <typename Fn>
  void for_each_until_lru(Fn&& fn) const {
    cache.for_each_until_lru([&fn](Handle_t h) { fn(h.get_key()); });
  }

  // For each item in the LRU list, call fn in MRU order until false
  template <typename Fn>
  void for_each_until_mru(Fn&& fn) const {
    cache.for_each_until_mru([&fn](Handle_t h) { fn(h.get_key()); });
  }

 protected:
  // The for-each APIs below are unsafe because they expose the entire
  // handle including size_idx; should only be called by friend classes
  template <typename Fn>
  void unsafe_for_each_lru(Fn&& fn) const {
    cache.for_each_lru(fn);
  }
  template <typename Fn>
  void unsafe_for_each_mru(Fn&& fn) const {
    cache.for_each_mru(fn);
  }
  template <typename Fn>
  void unsafe_for_each_until_lru(Fn&& fn) const {
    cache.for_each_until_lru(fn);
  }
  template <typename Fn>
  void unsafe_for_each_until_mru(Fn&& fn) const {
    cache.for_each_until_mru(fn);
  }

 public:
  std::ostream& print(std::ostream& os, int indent = 0);
  friend std::ostream& operator<<(std::ostream& os, GhostCache& c) {
    return c.print(os);
  }
};

// only sample 1/32 (~3.125%)
template <uint32_t SampleShift = 5, typename Hash = ghash,
          typename Meta = GhostMeta>
class SampledGhostCache : public GhostCache<Hash, Meta> {
 public:
  SampledGhostCache(uint32_t tick, uint32_t min_size, uint32_t max_size)
      : GhostCache<Hash, Meta>(tick >> SampleShift, min_size >> SampleShift,
                               max_size >> SampleShift) {
    assert(tick % (1 << SampleShift) == 0);
    assert(min_size % (1 << SampleShift) == 0);
    assert(max_size % (1 << SampleShift) == 0);
    // Left few bits used for sampling; right few used for hash.
    // Make sure they never overlap.
    assert(std::countr_zero<uint32_t>(std::bit_ceil<uint32_t>(max_size)) <=
           32 - static_cast<int>(SampleShift));
    assert(this->tick > 0);
  }

  // Only update ghost cache if the first few bits of hash is all zero
  void access(uint32_t block_id, AccessMode mode = AccessMode::DEFAULT) {
    uint32_t hash = Hash{}(block_id);
    if ((hash >> (32 - SampleShift)) == 0)
      this->access_impl(block_id, hash, mode);
  }

  [[nodiscard]] uint32_t get_tick() const { return this->tick << SampleShift; }
  [[nodiscard]] uint32_t get_min_size() const {
    return this->min_size << SampleShift;
  }
  [[nodiscard]] uint32_t get_max_size() const {
    return this->max_size << SampleShift;
  }

  [[nodiscard]] const CacheStat& get_stat(uint32_t cache_size) {
    return get_stat_shifted(cache_size >> SampleShift);
  }
  [[nodiscard]] double get_hit_rate(uint32_t cache_size) {
    return this->get_stat(cache_size).get_hit_rate();
  }
  [[nodiscard]] double get_miss_rate(uint32_t cache_size) {
    return this->get_stat(cache_size).get_miss_rate();
  }

 protected:
  template <uint32_t S, typename H>
  friend class SampledGhostKvCache;

  [[nodiscard]] const CacheStat& get_stat_shifted(uint32_t cache_size_shifted) {
    return GhostCache<Hash, Meta>::get_stat(cache_size_shifted);
  }
};

/**
 * When using ghost cache, we assume in_use list is always empty.
 */
template <typename Hash, typename Meta>
inline typename GhostCache<Hash, Meta>::Handle_t
GhostCache<Hash, Meta>::access_impl(uint32_t block_id, uint32_t hash,
                                    AccessMode mode) {
  Handle_t s;  // successor
  Handle_t h = cache.refresh(block_id, hash, s);
  assert(h);  // Since there is no handle in use, allocation must never fail.

  /**
   * To reason through the code below, consider an example where min_size=3,
   * max_size=7, tick=2, num_ticks=3.
   *              (LRU)                               (MRU)
   *  DummyHead <=> A <=> B <=> C <=> D <=> E <=> F <=> G.
   *  size_idx:     2,    2,    1,    1,    0,    0,    0.
   *  boundaries:              [1]         [0]
   *
   * If B is accessed, the list becomes:
   *  DummyHead <=> A <=> C <=> D <=> E <=> F <=> G <=> B.
   *  size_idx:     2,    2,    1,    1,    0,    0,    0.
   *  idx_changed:        ^           ^                 ^
   *  boundaries:              [1]         [0]
   *
   * If instead C is accessed, so the list becomes:
   *  DummyHead <=> A <=> B <=> D <=> E <=> F <=> G <=> C.
   *  size_idx:     2,    2,    1,    1,    0,    0,    0.
   *  idx_changed:                    ^                 ^
   *  boundaries:              [1]         [0]
   *
   * This means when X is accessed:
   * 1) every node at boundary with size_idx < X should increase its size_idx
   *    and the boundary pointer shall move to its next.
   * 2) X's size_idx should be set to 0.
   * 3) if X itself is a boundary, set that boundary to X's next (sucessor).
   */
  uint32_t size_idx;
  if (s) {  // No new insertion
    size_idx = h->size_idx;
    if (size_idx < num_ticks - 1 && boundaries[size_idx] == h.node)
      boundaries[size_idx] = s.node;
  } else {
    // New insertion happened may because
    // 1) even max_size cannot cache the block
    // 2) this block has never been accessed before
    // For simplicity, both cases are handled uniformly by treating it as a miss
    assert(cache.size() <= max_size);
    size_idx = cache.size() > min_size
                   ? (cache.size() - min_size + tick - 1) / tick
                   : 0;
    if (size_idx < num_ticks - 1 && cache.size() == size_idx * tick + min_size)
      boundaries[size_idx] = cache.lru_.next;
  }
  for (uint32_t i = 0; i < size_idx; ++i) {
    auto& b = boundaries[i];
    if (!b) continue;
    b->value.size_idx++;
    b = b->next;
  }
  h->size_idx = 0;

  switch (mode) {
    case AccessMode::DEFAULT:
      // if no successor,it must be a miss for all cache sizes
      if (s) ++reuse_distances[size_idx];
      ++reuse_count;
      break;
    case AccessMode::AS_MISS:
      ++reuse_count;
      break;
    case AccessMode::AS_HIT:
      ++reuse_distances[0];
      ++reuse_count;
      break;
    case AccessMode::NOOP:
      break;
  }
  return h;
}

template <typename Hash, typename Meta>
inline void GhostCache<Hash, Meta>::build_caches_stat() {
  uint32_t accum_hit_cnt = 0;
  for (size_t idx = 0; idx < caches_stat.size(); ++idx) {
    accum_hit_cnt += reuse_distances[idx];
    caches_stat[idx].hit_cnt = accum_hit_cnt;
    caches_stat[idx].miss_cnt = reuse_count - accum_hit_cnt;
  }
}

template <typename Hash, typename Meta>
inline std::ostream& GhostCache<Hash, Meta>::print(std::ostream& os,
                                                   int indent) {
  build_caches_stat();
  os << "GhostCache (tick=" << tick << ", min=" << min_size
     << ", max=" << max_size << ", num_ticks=" << num_ticks
     << ", size=" << cache.size() << ") {\n";

  for (int i = 0; i < indent + 1; ++i) os << '\t';
  os << "Boundaries: [";
  if (boundaries[0])
    os << boundaries[0]->key;
  else
    os << "(null)";
  for (uint32_t i = 1; i < boundaries.size(); ++i) {
    os << ", ";
    if (boundaries[i])
      os << boundaries[i]->key;
    else
      os << "(null)";
  }
  os << "]\n";
  for (int i = 0; i < indent + 1; ++i) os << '\t';
  os << "Stat:       [" << min_size << ": " << caches_stat[0];
  for (uint32_t i = 1; i < num_ticks; ++i)
    os << ", " << min_size + i * tick << ": " << caches_stat[i];
  os << "]\n";
  for (int i = 0; i < indent + 1; ++i) os << '\t';
  cache.print(os, indent + 1);
  for (int i = 0; i < indent; ++i) os << '\t';
  os << "}\n";
  return os;
}

}  // namespace gcache