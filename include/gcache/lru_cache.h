/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "handle.h"
#include "handle_table.h"

namespace gcache {

// Key_t should be lightweight that can be pass-by-value
// Value_t should be trivially copyable
template <typename Key_t, typename Value_t>
class LRUCache {
 public:
  typedef LRUHandle<Key_t, Value_t> Handle_t;
  LRUCache();
  LRUCache(size_t capacity) : LRUCache() { init_pool(capacity); };
  ~LRUCache();
  LRUCache(const LRUCache&) = delete;
  LRUCache(LRUCache&&) = delete;
  LRUCache& operator=(const LRUCache&) = delete;
  LRUCache& operator=(LRUCache&&) = delete;
  void init_pool(size_t capacity);

  // Insert a handle into cache with giveb key and hash if not exists; if does,
  // return the existing one
  // Returned handle must be released
  Handle_t* insert(Key_t key, uint32_t hash);
  // Search for a handle; return nullptr if not exist
  // Returned handle must be release
  Handle_t* lookup(Key_t key, uint32_t hash);
  // Release handle returned by insert/lookup
  void release(Handle_t* handle);

 private:
  Handle_t* alloc_handle();
  void free_handle(Handle_t* e);
  void list_remove(Handle_t* e);
  void list_append(Handle_t* list, Handle_t* e);
  void ref(Handle_t* e);
  void unref(Handle_t* e);

  // Initialized before use.
  size_t capacity_;

  // Manage batch of handle and place into the free list.
  // Allocate a handle from free_ and put it into table_; a handle in table_
  // must either present in lru_ or in_use_.
  Handle_t* pool_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  // Entries have refs==1.
  Handle_t lru_;

  // Dummy head of in-use list.
  // Entries are in use by clients and have refs >= 2.
  Handle_t in_use_;

  // Dummy head of free list.
  Handle_t free_;

  // Hash table to lookup
  HandleTable<Key_t, Value_t> table_;

 public:  // for debugging
  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const LRUCache& c) {
    return c.print(os);
  }
};

template <typename Key_t, typename Value_t>
LRUCache<Key_t, Value_t>::LRUCache() : capacity_(0), pool_(nullptr), table_() {
  // Make empty circular linked lists.
  lru_.next = &lru_;
  lru_.prev = &lru_;
  in_use_.next = &in_use_;
  in_use_.prev = &in_use_;
  // free_ will be initialized when init_pool() is called
}

template <typename Key_t, typename Value_t>
LRUCache<Key_t, Value_t>::~LRUCache() {
  assert(in_use_.next == &in_use_);  // Error if caller has an unreleased handle
  // Unnecessary for correctness, but kept to ease debugging
  for (const Handle_t* e = lru_.next; e != &lru_; e = e->next)
    assert(e->refs == 1);  // Invariant of lru_ list.
  delete[] pool_;
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t* LRUCache<Key_t, Value_t>::insert(
    Key_t key, uint32_t hash) {
  // Disable support for capacity_ == 0; the user must set capacity first
  assert(capacity_ > 0 && pool_);

  // Search to see if already exists
  Handle_t** ptr = table_.find_pointer(key, hash);
  Handle_t* e = *ptr;
  if (e) goto done;

  // Allocate a new handle
  e = alloc_handle();
  if (!e) return nullptr;
  e->init(key, hash);
  e->next_hash = nullptr;
  *ptr = e;

done:
  e->refs++;
  if (e->refs == 2) list_append(&in_use_, e);
  return e;
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t* LRUCache<Key_t, Value_t>::lookup(
    Key_t key, uint32_t hash) {
  Handle_t* e = table_.lookup(key, hash);
  if (e) ref(e);
  return e;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::release(Handle_t* handle) {
  unref(handle);
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::init_pool(size_t capacity) {
  assert(!capacity_ && !pool_);
  assert(capacity);
  capacity_ = capacity;
  pool_ = new Handle_t[capacity];
  // Put these entries into free list
  free_.next = &pool_[0];
  pool_[0].prev = &free_;
  free_.prev = &pool_[capacity - 1];
  pool_[capacity - 1].next = &free_;
  for (size_t i = 0; i < capacity - 1; ++i) {
    pool_[i].next = &pool_[i + 1];
    pool_[i + 1].prev = &pool_[i];
  }
  table_.reserve(capacity);
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t*
LRUCache<Key_t, Value_t>::alloc_handle() {
  if (free_.next != &free_) {  // Allocate from free list
    Handle_t* e = free_.next;
    list_remove(e);
    return e;
  }

  // Evict one handle from LRU and recycle it
  if (lru_.next == &lru_) return nullptr;  // No more space
  Handle_t* e = lru_.next;
  list_remove(e);  // remove from lru_
  assert(e->refs == 1);
  [[maybe_unused]] Handle_t* e_;
  e_ = table_.remove(e->key, e->hash);
  assert(e_ == e);
  return e;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::free_handle(Handle_t* e) {
  list_append(&free_, e);
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::ref(Handle_t* e) {
  if (e->refs == 1) {  // If on lru_ list, move to in_use_ list.
    list_remove(e);
    list_append(&in_use_, e);
  }
  e->refs++;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::unref(Handle_t* e) {
  assert(e->refs > 0);
  e->refs--;
  if (e->refs == 0) {  // Deallocate.
    free_handle(e);
  } else if (e->refs == 1) {
    // No longer in use; move to lru_ list.
    list_remove(e);
    list_append(&lru_, e);
  }
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::list_remove(Handle_t* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::list_append(Handle_t* list, Handle_t* e) {
  // Make "e" newest entry by inserting just before *list
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

template <typename Key_t, typename Value_t>
std::ostream& LRUCache<Key_t, Value_t>::print(std::ostream& os,
                                              int indent) const {
  os << "LRUCache (capacity=" << capacity_ << ") {\n";
  for (int i = 0; i < indent + 1; ++i) os << '\t';
  os << "lru:    [";
  lru_.print_list(os);
  os << "]\n";
  for (int i = 0; i < indent + 1; ++i) os << '\t';
  os << "in_use: [";
  in_use_.print_list(os);
  os << "]\n";
  for (int i = 0; i < indent + 1; ++i) os << '\t';
  table_.print(os, indent + 1);
  os << "}\n";
  return os;
}

}  // namespace gcache
