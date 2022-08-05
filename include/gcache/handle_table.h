/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once

#include "handle.h"
#include <cstdint>

namespace gcache {

// We provide our own simple hash table since it removes a whole bunch
// of porting hacks and is also faster than some of the built-in hash
// table implementations in some of the compiler/runtime combinations
// we have tested.  E.g., readrandom speeds up by ~5% over the g++
// 4.4.3's builtin hashtable.
template <typename Key_t, typename Value_t>
class HandleTable {
 public:
  typedef LRUHandle<Key_t, Value_t> Handle_t;

  HandleTable() : length_(0), elems_(0), list_(nullptr) { resize(); }
  ~HandleTable() { delete[] list_; }

  Handle_t* lookup(Key_t key, uint32_t hash);
  Handle_t* insert(Handle_t* h);
  Handle_t* remove(Key_t key, uint32_t hash);

 private:
  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  uint32_t length_;
  uint32_t elems_;
  Handle_t** list_;

  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  Handle_t** find_pointer(Key_t key, uint32_t hash);
  void resize();
};

template <typename Key_t, typename Value_t>
typename HandleTable<Key_t, Value_t>::Handle_t*
HandleTable<Key_t, Value_t>::lookup(Key_t key, uint32_t hash) {
  return *find_pointer(key, hash);
}

template <typename Key_t, typename Value_t>
typename HandleTable<Key_t, Value_t>::Handle_t*
HandleTable<Key_t, Value_t>::insert(Handle_t* h) {
  Handle_t** ptr = find_pointer(h->key, h->hash);
  Handle_t* old = *ptr;
  h->next_hash = (old == nullptr ? nullptr : old->next_hash);
  *ptr = h;
  if (old == nullptr) {
    ++elems_;
    if (elems_ > length_) {
      // Since each cache entry is fairly large, we aim for a small
      // average linked list length (<= 1).
      resize();
    }
  }
  return old;
}

template <typename Key_t, typename Value_t>
typename HandleTable<Key_t, Value_t>::Handle_t*
HandleTable<Key_t, Value_t>::remove(Key_t key, uint32_t hash) {
  Handle_t** ptr = find_pointer(key, hash);
  Handle_t* result = *ptr;
  if (result != nullptr) {
    *ptr = result->next_hash;
    --elems_;
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
void HandleTable<Key_t, Value_t>::resize() {
  uint32_t new_length = 4;
  while (new_length < elems_) {
    new_length *= 2;
  }
  Handle_t** new_list = new Handle_t*[new_length];
  memset(new_list, 0, sizeof(new_list[0]) * new_length);
  uint32_t count = 0;
  for (uint32_t i = 0; i < length_; i++) {
    Handle_t* h = list_[i];
    while (h != nullptr) {
      Handle_t* next = h->next_hash;
      uint32_t hash = h->hash;
      Handle_t** ptr = &new_list[hash & (new_length - 1)];
      h->next_hash = *ptr;
      *ptr = h;
      h = next;
      count++;
    }
  }
  assert(elems_ == count);
  delete[] list_;
  list_ = new_list;
  length_ = new_length;
}

}  // namespace gcache
