/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once

#include <cstdint>

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

// An entry is a variable length heap-allocated structure.  Entries
// are kept in a circular doubly linked list ordered by access time.
template <typename Key_t, typename Value_t>
class LRUHandle {
  LRUHandle* next_hash;
  LRUHandle* next;
  LRUHandle* prev;
  uint32_t refs;  // References, including cache reference, if present.
  uint32_t hash;  // Hash of key(); used for fast sharding and comparisons
  bool in_cache;  // Whether entry is in the cache.
  Key_t key;
  Value_t value;
};
}  // namespace gcache
