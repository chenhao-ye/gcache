/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once

#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "node.h"

namespace gcache {

// We provide our own simple hash table since it removes a whole bunch
// of porting hacks and is also faster than some of the built-in hash
// table implementations in some of the compiler/runtime combinations
// we have tested.  E.g., readrandom speeds up by ~5% over the g++
// 4.4.3's builtin hashtable.
template <typename Key_t, typename Value_t>
class NodeTable {
 private:
  using Node_t = LRUNode<Key_t, Value_t>;

 public:
  NodeTable() : length_(0), list_(nullptr) {}
  ~NodeTable() { delete[] list_; }

  void init(size_t size);  // size must be 2^n; must be called before any r/w

  // Caller must ensure e's key does not already present in table!
  void insert(Node_t* e);
  Node_t* lookup(Key_t key, uint32_t hash);
  Node_t* remove(Key_t key, uint32_t hash);

 private:
  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  Node_t** find_pointer(Key_t key, uint32_t hash);

  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  uint32_t length_;
  Node_t** list_;

 public:  // for debugging
  std::ostream& print(std::ostream& os, int indent = 0) const;
};

template <typename Key_t, typename Value_t>
inline void NodeTable<Key_t, Value_t>::insert(
    NodeTable<Key_t, Value_t>::Node_t* e) {
  // Caller must ensure e->key is not present in the table!
  assert(!lookup(e->key, e->hash));
  // Add to the head of this linked list
  Node_t** ptr = &list_[e->hash & (length_ - 1)];
  e->next_hash = *ptr;
  *ptr = e;
}

template <typename Key_t, typename Value_t>
inline typename NodeTable<Key_t, Value_t>::Node_t*
NodeTable<Key_t, Value_t>::lookup(Key_t key, uint32_t hash) {
  assert(length_ > 0);
  return *find_pointer(key, hash);
}

template <typename Key_t, typename Value_t>
inline typename NodeTable<Key_t, Value_t>::Node_t*
NodeTable<Key_t, Value_t>::remove(Key_t key, uint32_t hash) {
  assert(length_ > 0);
  Node_t** ptr = find_pointer(key, hash);
  Node_t* result = *ptr;
  if (result != nullptr) *ptr = result->next_hash;
  return result;
}

// Return a pointer to slot that points to a cache entry that
// matches key/hash.  If there is no such cache entry, return a
// pointer to the trailing slot in the corresponding linked list.
template <typename Key_t, typename Value_t>
inline typename NodeTable<Key_t, Value_t>::Node_t**
NodeTable<Key_t, Value_t>::find_pointer(Key_t key, uint32_t hash) {
  Node_t** ptr = &list_[hash & (length_ - 1)];
  while (*ptr != nullptr && ((*ptr)->hash != hash || key != (*ptr)->key)) {
    ptr = &(*ptr)->next_hash;
  }
  return ptr;
}

template <typename Key_t, typename Value_t>
inline void NodeTable<Key_t, Value_t>::init(size_t size) {
  size = std::bit_ceil<size_t>(size);
  length_ = size;
  list_ = new Node_t*[length_];
  memset(list_, 0, sizeof(list_[0]) * length_);
}

template <typename Key_t, typename Value_t>
inline std::ostream& NodeTable<Key_t, Value_t>::print(std::ostream& os,
                                                      int indent) const {
  os << "NodeTable (length=" << length_ << ") {\n";
  for (size_t i = 0; i < length_; ++i) {
    auto h = list_[i];
    if (!h) continue;
    for (int j = 0; j < indent; ++j) os << '\t';
    h->print_list_hash(os);
  }
  for (int j = 0; j < indent; ++j) os << '\t';
  os << "}\n";
  return os;
}

}  // namespace gcache
