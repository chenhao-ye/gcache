/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "handle.h"

namespace gcache {

template <typename Key_t, typename Value_t>
class LRUCache;

// We provide our own simple hash table since it removes a whole bunch
// of porting hacks and is also faster than some of the built-in hash
// table implementations in some of the compiler/runtime combinations
// we have tested.  E.g., readrandom speeds up by ~5% over the g++
// 4.4.3's builtin hashtable.
template <typename Key_t, typename Value_t>
class HandleTable {
 public:
  typedef LRUHandle<Key_t, Value_t> Handle_t;

  HandleTable() : length_(0), list_(nullptr) {}
  ~HandleTable() { delete[] list_; }

  Handle_t* lookup(Key_t key, uint32_t hash);
  Handle_t* remove(Key_t key, uint32_t hash);
  void reserve(size_t size);  // size must be 2^n
  // insertion requires a handle allocator and thus not provided as a built-in
  // API. Please build it on LRUCache with find_pointer().

 private:
  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  Handle_t** find_pointer(Key_t key, uint32_t hash);

  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  uint32_t length_;
  Handle_t** list_;

  friend class LRUCache<Key_t, Value_t>;

 public:  // for debugging
  friend std::ostream& operator<<(std::ostream& os, const HandleTable& ht) {
    os << "HandleTable (length=" << ht.length_ << ") {\n";
    for (size_t i = 0; i < ht.length_; ++i) {
      auto h = ht.list_[i];
      if (!h) continue;
      while (h) {
        os << '\t' << *h << ';';
        h = h->next_hash;
        if (i > 10) exit(0);
      }
      os << '\n';
    }
    os << "}\n";
    return os;
  }
};

template <typename Key_t, typename Value_t>
typename HandleTable<Key_t, Value_t>::Handle_t*
HandleTable<Key_t, Value_t>::lookup(Key_t key, uint32_t hash) {
  assert(length_ > 0);
  return *find_pointer(key, hash);
}

template <typename Key_t, typename Value_t>
typename HandleTable<Key_t, Value_t>::Handle_t*
HandleTable<Key_t, Value_t>::remove(Key_t key, uint32_t hash) {
  assert(length_ > 0);
  Handle_t** ptr = find_pointer(key, hash);
  Handle_t* result = *ptr;
  if (result != nullptr) {
    *ptr = result->next_hash;
  }
  return result;
}

// Return a pointer to slot that points to a cache entry that
// matches key/hash.  If there is no such cache entry, return a
// pointer to the trailing slot in the corresponding linked list.
template <typename Key_t, typename Value_t>
typename HandleTable<Key_t, Value_t>::Handle_t**
HandleTable<Key_t, Value_t>::find_pointer(Key_t key, uint32_t hash) {
  Handle_t** ptr = &list_[hash & (length_ - 1)];
  while (*ptr != nullptr && ((*ptr)->hash != hash || key != (*ptr)->key)) {
    ptr = &(*ptr)->next_hash;
  }
  return ptr;
}

template <typename Key_t, typename Value_t>
void HandleTable<Key_t, Value_t>::reserve(size_t size) {
  size = std::bit_ceil<size_t>(size);
  length_ = size;
  list_ = new Handle_t*[length_];
  memset(list_, 0, sizeof(list_[0]) * length_);
}

}  // namespace gcache
