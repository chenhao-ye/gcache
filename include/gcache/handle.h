/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

namespace gcache {
// LRU cache implementation
//
// Cache entries have an "in_cache" boolean indicating whether the cache has a
// reference on the entry.  The only ways that this can become false without the
// entry being passed to its "deleter" are via erase(), via insert() when
// an element with a duplicate key is inserted, or on destruction of the cache.
//
// The cache keeps two linked lists of items in the cache.  All items in the
// cache are in one list or the other, and never both.  Items still referenced
// by clients but erased from the cache are in neither list.  The lists are:
// - in-use:  contains the items currently referenced by clients, in no
//   particular order.  (This list is used for invariant checking.  If we
//   removed the check, elements that would otherwise be on this list could be
//   left as disconnected singleton lists.)
// - LRU:  contains the items not currently referenced by clients, in LRU order
// Elements are moved between these lists by the Ref() and Unref() methods,
// when they detect an element in the cache acquiring or losing its only
// external reference.

template <typename Key_t, typename Value_t>
class HandleTable;
template <typename Key_t, typename Value_t>
class LRUCache;
class GhostCache;

typedef uint32_t handle_idx_t;
constexpr static const uint32_t INVALID_IDX =
    std::numeric_limits<uint32_t>::max();

// An entry is a variable length heap-allocated structure.  Entries
// are kept in a circular doubly linked list ordered by access time.
template <typename Key_t, typename Value_t>
class LRUHandle {
 private:
  handle_idx_t next_hash;
  handle_idx_t next;
  handle_idx_t prev;
  uint32_t refs;  // References, including cache reference, if present.

  friend class HandleTable<Key_t, Value_t>;
  friend class LRUCache<Key_t, Value_t>;
  friend class GhostCache;

 public:
  uint32_t hash;  // Hash of key; used for fast sharding and comparisons
  Key_t key;
  Value_t value;

  void init(Key_t key, uint32_t hash) {
    this->refs = 1;
    this->hash = hash;
    this->key = key;
  }

  // print for debugging
  friend std::ostream& operator<<(std::ostream& os, const LRUHandle& h) {
    return os << h.key << ": " << h.value << " (refs=" << h.refs
              << ", hash=" << h.hash << ")";
  }
};

static_assert(sizeof(LRUHandle<uint32_t, void*>) == 32,
              "Expect Handle<uint32_t, void*> to be exactly 32-byte");
// NOTE: Though 32-byte seems more cache-friendly, we find in our benchmark,
// LRUCache<uint32_t, uint32_t> has better performance than LRUCache<uint32_t,
// uint64_t>.

}  // namespace gcache
