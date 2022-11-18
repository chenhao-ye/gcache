#pragma once

#include <nmmintrin.h>  // for _mm_crc32_u32 instruction

#include <bit>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <vector>

#include "gcache/handle.h"
#include "gcache/handle_table.h"
#include "gcache/lru_cache.h"

namespace gcache {

/**
 * Here we tested on several implementation of hash. The exposed one is defined
 * as a macro `gcache_hash()`.
 */

// From XXHash:
// https://github.com/Cyan4973/xxHash/blob/release/xxhash.h#L1968
[[maybe_unused]] static inline uint32_t xxhash_u32(uint32_t x) {
  x ^= x >> 15;
  x *= 0x85EBCA77U;
  x ^= x >> 13;
  x *= 0xC2B2AE3DU;
  x ^= x >> 16;
  return x;
}

// From MurmurHash:
// https://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp#L68
[[maybe_unused]] static inline uint32_t murmurhash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x85ebca6b;
  x ^= x >> 13;
  x *= 0xc2b2ae35;
  x ^= x >> 16;
  return x;
}

// From CRC
#define crc_u32(x) _mm_crc32_u32(/*seed*/ 0x537, x)

#define gcache_hash(x) crc_u32(x)

struct CacheStat {
  uint64_t hit_cnt;
  uint64_t miss_cnt;

 public:
  CacheStat() : hit_cnt(0), miss_cnt(0) {}
  void add_hit() { ++hit_cnt; }
  void add_miss() { ++miss_cnt; }

  // we may read an inconsistent version if the reader thread is not the writer,
  // but most inaccuracy is tolerable, unless it produces a unreasonable value,
  // e.g., hit_rate > 100%.
  // we don't use atomic here because we find it is too expensive.
  double get_hit_rate() const {
    uint64_t acc_cnt = hit_cnt + miss_cnt;
    if (acc_cnt) return std::numeric_limits<double>::infinity();
    return double(hit_cnt) / double(acc_cnt);
  }
  void reset() {
    hit_cnt = 0;
    miss_cnt = 0;
  }

  std::ostream& print(std::ostream& os, int width = 0) const {
    uint64_t acc_cnt = hit_cnt + miss_cnt;
    if (acc_cnt == 0)
      return os << "    NAN (" << std::setw(width) << std::fixed << hit_cnt
                << '/' << std::setw(width) << std::fixed << acc_cnt << ')';
    return os << std::setw(6) << std::fixed << std::setprecision(2)
              << get_hit_rate() * 100 << "% (" << std::setw(width) << std::fixed
              << hit_cnt << '/' << std::setw(width) << std::fixed << acc_cnt
              << ')';
  }

  // print for debugging
  friend std::ostream& operator<<(std::ostream& os, const CacheStat& s) {
    return s.print(os, 0);
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

  using Handle_t = LRUCache<uint32_t, uint32_t>::Handle_t;
  using Node_t = LRUCache<uint32_t, uint32_t>::Node_t;
  // these must be placed after num_ticks to ensure a correct ctor order
  std::vector<Node_t*> size_boundaries;
  std::vector<CacheStat> caches_stat;

  void access_impl(uint32_t page_id, uint32_t hash);

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
  void access(uint32_t page_id) { access_impl(page_id, gcache_hash(page_id)); }

  uint32_t get_tick() const { return tick; }
  uint32_t get_min_size() const { return min_size; }
  uint32_t get_max_size() const { return max_size; }

  double get_hit_rate(uint32_t cache_size) const {
    return get_stat(cache_size).get_hit_rate();
  }

  const CacheStat& get_stat(uint32_t cache_size) const {
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

// only sample 1/32 (~3%)
template <uint32_t SampleShift = 5>
class SampleGhostCache : public GhostCache {
 public:
  SampleGhostCache(uint32_t tick, uint32_t min_size, uint32_t max_size)
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
  void access(uint32_t page_id) {
    uint32_t hash = gcache_hash(page_id);
    if ((hash >> (32 - SampleShift)) == 0) access_impl(page_id, hash);
  }

  uint32_t get_tick() const { return tick << SampleShift; }
  uint32_t get_min_size() const { return min_size << SampleShift; }
  uint32_t get_max_size() const { return max_size << SampleShift; }

  double get_hit_rate(uint32_t cache_size) const {
    return get_stat(cache_size).get_hit_rate();
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
inline void GhostCache::access_impl(uint32_t page_id, uint32_t hash) {
  uint32_t size_idx;
  Handle_t s;  // successor
  Handle_t h = cache.touch(page_id, hash, s);
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
  if (s) {
    for (uint32_t i = 0; i < size_idx; ++i) caches_stat[i].add_miss();
    for (uint32_t i = size_idx; i < num_ticks; ++i) caches_stat[i].add_hit();
  } else {
    for (uint32_t i = 0; i < num_ticks; ++i) caches_stat[i].add_miss();
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