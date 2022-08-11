#pragma once

#include <bit>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <vector>

#include "gcache/handle.h"
#include "gcache/handle_table.h"
#include "gcache/lru_cache.h"

namespace gcache {

// A hash implementation from XXHash:
// https://github.com/Cyan4973/xxHash/blob/release/xxhash.h#L1968
uint32_t fast_hash(uint32_t x) {
  x ^= x >> 15;
  x *= 0x85EBCA77U;
  x ^= x >> 13;
  x *= 0xC2B2AE3DU;
  x ^= x >> 16;
  return x;
}

class CacheStat {
  uint64_t acc_cnt;
  uint64_t hit_cnt;

 public:
  void add_hit() {
    acc_cnt++;
    hit_cnt++;
  }
  void add_miss() { acc_cnt++; }
  double get_hit_rate() const { return double(hit_cnt) / double(acc_cnt); }

  // print for debugging
  friend std::ostream& operator<<(std::ostream& os, const CacheStat& s) {
    return os << std::setprecision(3) << s.get_hit_rate() << " (" << s.hit_cnt
              << '/' << s.acc_cnt << ')';
  }
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

  // Key is page_id/block number
  // Value is "size_idx", which means the handle will in cache if the cache size
  // is ((size_idx + 1) * tick) but not if the cache size is (size_idx * tick).
  LRUCache<uint32_t, uint32_t> cache;

  typedef LRUCache<uint32_t, uint32_t>::Handle_t Handle_t;
  // these must be placed after num_ticks to ensure a correct ctor order
  std::vector<Handle_t*> size_boundaries;
  std::vector<CacheStat> caches_stat;

  void access_impl(uint32_t page_id, uint32_t hash);

 public:
  GhostCache(uint32_t tick, uint32_t begin_size, uint32_t end_size)
      : tick(tick),
        min_size(begin_size),
        max_size(((end_size - begin_size) / tick) * tick + begin_size),
        num_ticks((end_size - begin_size) / tick + 1),
        lru_size(0),
        cache(),
        size_boundaries(num_ticks, nullptr),
        caches_stat(num_ticks) {
    assert(tick > 0);
    assert(begin_size > 1);  // otherwise the first boundary will be LRU evicted
    assert(min_size + (num_ticks - 1) * tick == max_size);
    cache.init(max_size);
  }
  void access(uint32_t page_id) { access_impl(page_id, fast_hash(page_id)); }

  double get_hit_rate(uint32_t cache_size) {
    assert((cache_size - min_size) % tick == 0);
    uint32_t size_idx = (cache_size - min_size) / tick;
    return caches_stat[size_idx].get_hit_rate();
  }

  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const GhostCache& c) {
    return c.print(os);
  }
};

// only sample 1/32 (~3%)
template <uint32_t SampleShift = 5>
class SampleGhostCache : public GhostCache {
 public:
  SampleGhostCache(uint32_t tick, uint32_t begin_size, uint32_t end_size)
      : GhostCache(tick >> SampleShift, begin_size >> SampleShift,
                   end_size >> SampleShift) {
    assert(tick % (1 << SampleShift) == 0);
    assert(begin_size % (1 << SampleShift) == 0);
    assert(end_size % (1 << SampleShift) == 0);
    // Left few bits used for sampling; right few used for hash.
    // Make sure they never overlap.
    assert(std::countr_zero<uint32_t>(std::bit_ceil<uint32_t>(max_size)) <=
           32 - static_cast<int>(SampleShift));
  }

  // Only update ghost cache if the first few bits of hash is all zero
  void access(uint32_t page_id) {
    uint32_t hash = fast_hash(page_id);
    if ((hash >> (32 - SampleShift)) == 0) access_impl(page_id, hash);
  }

  double get_hit_rate(uint32_t cache_size) {
    cache_size >>= SampleShift;
    assert((cache_size - min_size) % tick == 0);
    uint32_t size_idx = (cache_size - min_size) / tick;
    return caches_stat[size_idx].get_hit_rate();
  }
};

/**
 * When using ghost cache, we assume in_use list is always empty.
 */
void GhostCache::access_impl(uint32_t page_id, uint32_t hash) {
  uint32_t size_idx;
  Handle_t* s;  // successor
  Handle_t* h = cache.touch(page_id, hash, s);
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
    size_idx = h->value;
    if (size_idx < num_ticks && size_boundaries[size_idx] == h)
      size_boundaries[size_idx] = s;
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
  if (s) {
    for (uint32_t i = 0; i < size_idx; ++i) caches_stat[i].add_miss();
    for (uint32_t i = size_idx; i < num_ticks; ++i) caches_stat[i].add_hit();
  } else {
    for (uint32_t i = 0; i < num_ticks; ++i) caches_stat[i].add_miss();
  }

  h->value = 0;
}

std::ostream& GhostCache::print(std::ostream& os, int indent) const {
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