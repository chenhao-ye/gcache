/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once

#include <cstdint>
#include <iostream>

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

// An entry is a variable length heap-allocated structure.  Entries
// are kept in a circular doubly linked list ordered by access time.
template <typename Key_t, typename Value_t>
class LRUHandle {
 private:
  LRUHandle* next_hash;
  LRUHandle* next;
  LRUHandle* prev;
  uint32_t refs;  // References, including cache reference, if present.

  friend class HandleTable<Key_t, Value_t>;
  friend class LRUCache<Key_t, Value_t>;

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
  // print a list; this must be a dummpy list head
  std::ostream& print_list(std::ostream& os) const {
    os << this->key;
    auto h = next;
    while (h != this) {
      os << ", " << h->key;
      assert(h == h->next->prev);
      h = h->next;
    }
    return os;
  }
  std::ostream& print_list_hash(std::ostream& os) const {
    auto h = this;
    while (h) {
      os << '\t' << *h << ';';
      h = h->next_hash;
    }
    os << '\n';
    return os;
  }
};
}  // namespace gcache
