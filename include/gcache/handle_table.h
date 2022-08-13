/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once

#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "handle.h"

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
  HandleTable() : length_(0), list_(nullptr) {}
  ~HandleTable() { delete[] list_; }

  // Caller must ensure e's key does not already present in table!
  void insert(Handle_t* e);
  Handle_t* lookup(Key_t key, uint32_t hash);
  Handle_t* remove(Key_t key, uint32_t hash);
  // size must be 2^n; must be called before any r/w
  void reserve(size_t size, Handle_t* pool);

 private:
  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  handle_idx_t* find_pointer(Key_t key, uint32_t hash);

  handle_idx_t ptr_to_idx(const Handle_t* e) const {
    assert(e != nullptr);
    return e - pool_;
  }
  Handle_t* idx_to_ptr(const handle_idx_t idx) const {
    assert(idx != INVALID_IDX);
    return &pool_[idx];
  }

  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  uint32_t length_;
  handle_idx_t* list_;
  Handle_t* pool_;  // allocated by LRUCache; kept for idx-ptr translation

 public:  // for debugging
  std::ostream& print(std::ostream& os, int indent = 0) const;
  std::ostream& print_list_hash(std::ostream& os, const Handle_t* list) const;
};

template <typename Key_t, typename Value_t>
void HandleTable<Key_t, Value_t>::insert(Handle_t* e) {
  // Caller must ensure e->key does not present in the table!
  assert(!lookup(e->key, e->value));
  // Add to the head of this linked list
  handle_idx_t* ptr = &list_[e->hash & (length_ - 1)];
  e->next_hash = *ptr;
  *ptr = ptr_to_idx(e);
}

template <typename Key_t, typename Value_t>
typename HandleTable<Key_t, Value_t>::Handle_t*
HandleTable<Key_t, Value_t>::lookup(Key_t key, uint32_t hash) {
  assert(length_ > 0);
  handle_idx_t idx = *find_pointer(key, hash);
  if (idx == INVALID_IDX) return nullptr;
  return idx_to_ptr(idx);
}

template <typename Key_t, typename Value_t>
typename HandleTable<Key_t, Value_t>::Handle_t*
HandleTable<Key_t, Value_t>::remove(Key_t key, uint32_t hash) {
  assert(length_ > 0);
  handle_idx_t* ptr = find_pointer(key, hash);
  if (*ptr == INVALID_IDX) return nullptr;
  Handle_t* result = idx_to_ptr(*ptr);
  *ptr = result->next_hash;
  return result;
}

// Return a pointer to slot that points to a cache entry that
// matches key/hash.  If there is no such cache entry, return a
// pointer to the trailing slot in the corresponding linked list.
template <typename Key_t, typename Value_t>
handle_idx_t* HandleTable<Key_t, Value_t>::find_pointer(Key_t key,
                                                        uint32_t hash) {
  handle_idx_t* ptr = &list_[hash & (length_ - 1)];
  while (*ptr != INVALID_IDX) {
    Handle_t* e = idx_to_ptr(*ptr);
    if (e->hash == hash && key == e->key) break;
    ptr = &e->next_hash;
  }
  return ptr;
}

template <typename Key_t, typename Value_t>
void HandleTable<Key_t, Value_t>::reserve(size_t size, Handle_t* pool) {
  size = std::bit_ceil<size_t>(size);
  length_ = size;
  list_ = new handle_idx_t[length_];
  for (uint32_t i = 0; i < length_; ++i) list_[i] = INVALID_IDX;
  pool_ = pool;
}

template <typename Key_t, typename Value_t>
std::ostream& HandleTable<Key_t, Value_t>::print(std::ostream& os,
                                                 int indent) const {
  os << "HandleTable (length=" << length_ << ") {\n";
  for (size_t i = 0; i < length_; ++i) {
    auto idx = list_[i];
    if (idx == INVALID_IDX) continue;
    for (int j = 0; j < indent; ++j) os << '\t';
    print_list_hash(os, idx_to_ptr(idx));
  }
  for (int j = 0; j < indent; ++j) os << '\t';
  os << "}\n";
  return os;
}

template <typename Key_t, typename Value_t>
std::ostream& HandleTable<Key_t, Value_t>::print_list_hash(
    std::ostream& os, const Handle_t* list) const {
  handle_idx_t idx = ptr_to_idx(list);
  while (idx != INVALID_IDX) {
    Handle_t* e = idx_to_ptr(idx);
    os << '\t' << *e << ';';
    idx = e->next_hash;
  }
  os << '\n';
  return os;
}

}  // namespace gcache
