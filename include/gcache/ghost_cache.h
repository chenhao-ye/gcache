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

/**
 * Simulate a set of page cache, where each page only carry thin metadata
 * Templated type Meta must have a field size_idx; in almost all cases, this
 * field does not need to specified; it is only useful if there is some
 * additional per-page metadata to be carried.
 */
template <typename Hash = ghash, typename Meta = GhostMeta>
class GhostCache {
 protected:
  uint32_t tick;
  uint32_t min_size;
  uint32_t max_size;
  uint32_t num_ticks;

  // Key is block_id/block number
  // Value is "size_idx", which is the least non-negative number such that the
  // key will in cache if the cache size is (size_idx * tick) + min_size
  LRUCache<uint32_t, Meta, Hash> cache;

  using Handle_t = typename LRUCache<uint32_t, Meta, Hash>::Handle_t;
  using Node_t = typename LRUCache<uint32_t, Meta, Hash>::Node_t;
  // these must be placed after num_ticks to ensure a correct ctor order
  std::vector<Node_t*> boundaries;
  std::vector<CacheStat> caches_stat;

  void access_impl(uint32_t block_id, uint32_t hash, AccessMode mode);

 public:
  GhostCache(uint32_t tick, uint32_t min_size, uint32_t max_size)
      : tick(tick),
        min_size(min_size),
        max_size(max_size),
        num_ticks((max_size - min_size) / tick + 1),
        cache(),
        boundaries(num_ticks - 1, nullptr),
        caches_stat(num_ticks) {
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

  // For each item in the LRU list, call fn(key) in LRU order
  template <typename Fn>
  void for_each_lru(Fn&& fn) {
    cache.for_each_lru([&fn](Handle_t h) { fn(h.get_key()); });
  }

  // For each item in the LRU list, call fn(key) in LRU order
  template <typename Fn>
  void for_each_mru(Fn&& fn) {
    cache.for_each_mru([&fn](Handle_t h) { fn(h.get_key()); });
  }

  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const GhostCache& c) {
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

  uint32_t get_tick() const { return this->tick << SampleShift; }
  uint32_t get_min_size() const { return this->min_size << SampleShift; }
  uint32_t get_max_size() const { return this->max_size << SampleShift; }

  double get_hit_rate(uint32_t cache_size) const {
    return get_stat(cache_size).get_hit_rate();
  }
  double get_miss_rate(uint32_t cache_size) const {
    return get_stat(cache_size).get_miss_rate();
  }

  const CacheStat& get_stat(uint32_t cache_size) const {
    cache_size >>= SampleShift;
    assert(cache_size >= this->min_size);
    assert(cache_size <= this->max_size);
    assert((cache_size - this->min_size) % this->tick == 0);
    uint32_t size_idx = (cache_size - this->min_size) / this->tick;
    assert(size_idx < this->num_ticks);
    return this->caches_stat[size_idx];
  }
};

/**
 * When using ghost cache, we assume in_use list is always empty.
 */
template <typename Hash, typename Meta>
inline void GhostCache<Hash, Meta>::access_impl(uint32_t block_id,
                                                uint32_t hash,
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
}

template <typename Hash, typename Meta>
inline std::ostream& GhostCache<Hash, Meta>::print(std::ostream& os,
                                                   int indent) const {
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