#include <bits/stdint-uintn.h>
#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "gcache/handle.h"
#include "gcache/handle_table.h"
#include "gcache/lru_cache.h"

namespace gcache {

struct CacheStat {
  uint64_t acc_cnt;
  uint64_t hit_cnt;
};

/**
 * Simulate a set of page cache.
 */
class GhostCache {
  uint32_t tick;
  uint32_t min_size;
  uint32_t max_size;  // inclusive
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

 public:
  GhostCache(uint32_t tick, uint32_t begin_size, uint32_t end_size)
      : tick(tick),
        min_size(begin_size),
        max_size(((end_size - begin_size) / tick - 1) * tick + begin_size),
        num_ticks((end_size - begin_size) / tick),
        lru_size(0),
        cache(),
        size_boundaries(num_ticks, nullptr) {
    cache.init(max_size);
  }

  /**
   * When using ghost cache, we assume in_use list is always empty.
   */
  void access(uint32_t page_id) {
    uint32_t hash = static_cast<uint32_t>(std::hash<uint32_t>()(page_id));
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
      size_idx = (lru_size - min_size) / tick;
      if (size_idx < num_ticks &&
          lru_size - min_size + 1 == (size_idx + 1) * tick)
        size_boundaries[size_idx] = cache.lru_.next;
    }
    for (uint32_t i = 0; i < size_idx; ++i) {
      auto& b = size_boundaries[i];
      b->value++;
      b = b->next;
    }
    h->value = 0;
  }
};
}  // namespace gcache