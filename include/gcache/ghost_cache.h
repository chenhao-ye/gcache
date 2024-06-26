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

/**
 * Simulate a set of page cache.
 */
class GhostCache {
 protected:
  uint32_t tick;
  uint32_t min_size;
  uint32_t max_size;
  uint32_t num_ticks;
  uint32_t lru_size;  // Number of handles in cache LRU right now

  // Key is block_id/block number
  // Value is "size_idx", which means the handle will in cache if the cache size
  // is ((size_idx + 1) * tick) but not if the cache size is (size_idx * tick).
  LRUCache<uint32_t, uint32_t, ghash> cache;

  using Handle_t = LRUCache<uint32_t, uint32_t, ghash>::Handle_t;
  using Node_t = LRUCache<uint32_t, uint32_t, ghash>::Node_t;
  // these must be placed after num_ticks to ensure a correct ctor order
  std::vector<Node_t*> size_boundaries;
  std::vector<CacheStat> caches_stat;

  void access_impl(uint32_t block_id, uint32_t hash, AccessMode mode);

 public:
  GhostCache(uint32_t tick, uint32_t min_size, uint32_t max_size)
      : tick(tick),
        min_size(min_size),
        max_size(max_size),
        num_ticks((max_size - min_size) / tick + 1),
        lru_size(0),
        cache(),
        size_boundaries(num_ticks, nullptr),
        caches_stat(num_ticks) {
    assert(tick > 0);
    assert(min_size > 1);  // otherwise the first boundary will be LRU evicted
    assert(min_size + (num_ticks - 1) * tick == max_size);
    cache.init(max_size);
  }
  void access(uint32_t block_id, AccessMode mode = AccessMode::DEFAULT) {
    access_impl(block_id, ghash{}(block_id), mode);
  }

  [[nodiscard]] uint32_t get_tick() const { return tick; }
  [[nodiscard]] uint32_t get_min_size() const { return min_size; }
  [[nodiscard]] uint32_t get_max_size() const { return max_size; }

  [[nodiscard]] double get_hit_rate(uint32_t cache_size) const {
    return get_stat(cache_size).get_hit_rate();
  }
  [[nodiscard]] double get_miss_rate(uint32_t cache_size) const {
    return get_stat(cache_size).get_miss_rate();
  }

  [[nodiscard]] const CacheStat& get_stat(uint32_t cache_size) const {
    assert(cache_size >= min_size);
    assert(cache_size <= max_size);
    assert((cache_size - min_size) % tick == 0);
    uint32_t size_idx = (cache_size - min_size) / tick;
    assert(size_idx < num_ticks);
    return caches_stat[size_idx];
  }

  void reset_stat() {
    for (auto& s : caches_stat) s.reset();
  }

  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const GhostCache& c) {
    return c.print(os);
  }
};

// only sample 1/32 (~3.125%)
template <uint32_t SampleShift = 5>
class SampledGhostCache : public GhostCache {
 public:
  SampledGhostCache(uint32_t tick, uint32_t min_size, uint32_t max_size)
      : GhostCache(tick >> SampleShift, min_size >> SampleShift,
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
    uint32_t hash = ghash{}(block_id);
    if ((hash >> (32 - SampleShift)) == 0) access_impl(block_id, hash, mode);
  }

  uint32_t get_tick() const { return tick << SampleShift; }
  uint32_t get_min_size() const { return min_size << SampleShift; }
  uint32_t get_max_size() const { return max_size << SampleShift; }

  double get_hit_rate(uint32_t cache_size) const {
    return get_stat(cache_size).get_hit_rate();
  }
  double get_miss_rate(uint32_t cache_size) const {
    return get_stat(cache_size).get_miss_rate();
  }

  const CacheStat& get_stat(uint32_t cache_size) const {
    cache_size >>= SampleShift;
    assert(cache_size >= min_size);
    assert(cache_size <= max_size);
    assert((cache_size - min_size) % tick == 0);
    uint32_t size_idx = (cache_size - min_size) / tick;
    assert(size_idx < num_ticks);
    return caches_stat[size_idx];
  }
};

/**
 * When using ghost cache, we assume in_use list is always empty.
 */
inline void GhostCache::access_impl(uint32_t block_id, uint32_t hash,
                                    AccessMode mode) {
  uint32_t size_idx;
  Handle_t s;  // successor
  Handle_t h = cache.refresh(block_id, hash, s);
  assert(h);  // Since there is no handle in use, allocation must never fail.

  /**
   * To reason through the code below, consider an example where min_size=1,
   * max_size=5, tick=2.
   *  DummyHead <=> A <=> B <=> C <=> D <=> E, where E is MRU and A is LRU.
   *  size_idx:     2,    1,    1,    0,    0.
   *  size_boundaries:   [1]         [0]
   *
   * Now B is accessed, so the list becomes:
   *  DummyHead <=> A <=> C <=> D <=> E <=> B.
   *  size_idx:     2,    1,    1,    0,    0.
   *  size_boundaries:   [1]         [0]
   * To get to this state,
   *  1) boundaries with size_idx < B should move to its next and increase
   *     its size_idx
   *  2) if B itself is a boundary, set that boundary to B's sucessor.
   */
  if (s) {  // No new insertion
    size_idx = *h;
    if (size_idx < num_ticks && size_boundaries[size_idx] == h.node)
      size_boundaries[size_idx] = s.node;
  } else {
    assert(lru_size <= max_size);
    if (lru_size < max_size) ++lru_size;
    if (lru_size <= min_size)
      size_idx = 0;
    else
      size_idx = (lru_size - min_size + tick - 1) / tick;
    if (size_idx < num_ticks && lru_size - min_size == size_idx * tick)
      size_boundaries[size_idx] = cache.lru_.next;
  }
  for (uint32_t i = 0; i < size_idx; ++i) {
    auto& b = size_boundaries[i];
    if (!b) continue;
    b->value++;
    b = b->next;
  }
  switch (mode) {
    case AccessMode::DEFAULT:
      if (s) {
        for (uint32_t i = 0; i < size_idx; ++i) caches_stat[i].add_miss();
        for (uint32_t i = size_idx; i < num_ticks; ++i)
          caches_stat[i].add_hit();
      } else {
        for (uint32_t i = 0; i < num_ticks; ++i) caches_stat[i].add_miss();
      }
      break;
    case AccessMode::AS_MISS:
      for (uint32_t i = 0; i < num_ticks; ++i) caches_stat[i].add_miss();
      break;
    case AccessMode::AS_HIT:
      for (uint32_t i = 0; i < num_ticks; ++i) caches_stat[i].add_hit();
      break;
    case AccessMode::NOOP:
      break;
  }

  *h = 0;
}

inline std::ostream& GhostCache::print(std::ostream& os, int indent) const {
  os << "GhostCache (tick=" << tick << ", min=" << min_size
     << ", max=" << max_size << ", num_ticks=" << num_ticks
     << ", lru_size=" << lru_size << ") {\n";

  for (int i = 0; i < indent + 1; ++i) os << '\t';
  os << "Boundaries: [";
  if (size_boundaries[0])
    os << size_boundaries[0]->key;
  else
    os << "(null)";
  for (uint32_t i = 1; i < num_ticks; ++i) {
    os << ", ";
    if (size_boundaries[i])
      os << size_boundaries[i]->key;
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