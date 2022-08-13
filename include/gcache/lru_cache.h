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
  ~LRUCache();
  LRUCache(const LRUCache&) = delete;
  LRUCache(LRUCache&&) = delete;
  LRUCache& operator=(const LRUCache&) = delete;
  LRUCache& operator=(LRUCache&&) = delete;
  void init(size_t capacity);

  // Set pin to be true to pin the returned Handle so it won't be recycled by
  // LRU; a pinned handle must be unpinned later by calling release()

  // Insert a handle into cache with giveb key and hash if not exists; if does,
  // return the existing one
  Handle_t* insert(Key_t key, uint32_t hash, bool pin = false);
  // Search for a handle; return nullptr if not exist
  Handle_t* lookup(Key_t key, uint32_t hash, bool pin = false);
  // Release pinned handle returned by insert/lookup
  void release(Handle_t* handle);
  // Similar to insert but 1) never pin and the targeted handle must be in LRU
  // list, 2) return `successor`: the handle with the same order as the returned
  // handle after LRU operations (nullptr if newly inserted).
  Handle_t* touch(Key_t key, uint32_t hash, Handle_t*& successor);

  // This is to get the least-recently-used handle, not the list!
  Handle_t* get_lru_handle() const { return idx_to_ptr(lru_->next); }

  Handle_t* get_list_next(const Handle_t* e) const {
    return idx_to_ptr(e->next);
  }
  Handle_t* get_list_prev(const Handle_t* e) const {
    return idx_to_ptr(e->prev);
  }

 private:
  Handle_t* alloc_handle();
  void free_handle(Handle_t* e);
  void list_remove(Handle_t* e);
  void list_append(Handle_t* list, Handle_t* e);
  void ref(Handle_t* e);
  void unref(Handle_t* e);
  // Perform LRU operation and return the handle with the same order in the list
  // after LRU (usually it's e->next)
  Handle_t* lru_refresh(Handle_t* e);

  handle_idx_t ptr_to_idx(const Handle_t* e) const {
    assert(e != nullptr);
    return e - pool_;
  }
  Handle_t* idx_to_ptr(const handle_idx_t idx) const {
    assert(idx != INVALID_IDX);
    return &pool_[idx];
  }

  // Initialized before use.
  size_t capacity_;

  // Manage batch of handle and place into the free list.
  // Allocate a handle from free_ and put it into table_; a handle in table_
  // must either present in lru_ or in_use_.
  Handle_t* pool_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  // Entries have refs==1.
  Handle_t* lru_;

  // Dummy head of in-use list.
  // Entries are in use by clients and have refs >= 2.
  Handle_t* in_use_;

  // Dummy head of free list.
  Handle_t* free_;

  // Hash table to lookup
  HandleTable<Key_t, Value_t> table_;

 public:  // for debugging
  std::ostream& print_list(std::ostream& os, const Handle_t* list) const;
  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const LRUCache& c) {
    return c.print(os);
  }
};

template <typename Key_t, typename Value_t>
LRUCache<Key_t, Value_t>::LRUCache() : capacity_(0), pool_(nullptr), table_() {}
// lru_, in_use_, free_ will be initialized when init() is called

template <typename Key_t, typename Value_t>
LRUCache<Key_t, Value_t>::~LRUCache() {
  // Error if caller has an unreleased handle
  assert(get_list_next(in_use_) == in_use_);
  // Unnecessary for correctness, but kept to ease debugging
  for (auto e = get_list_next(lru_); e != lru_; e = get_list_next(e))
    assert(e->refs == 1);  // Invariant of lru_ list.
  delete[] pool_;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::init(size_t capacity) {
  assert(!capacity_ && !pool_);
  assert(capacity);
  capacity_ = capacity;
  pool_ = new (std::align_val_t(64)) Handle_t[capacity + 3];

  free_ = &pool_[capacity];
  lru_ = &pool_[capacity + 1];
  in_use_ = &pool_[capacity + 2];

  // Initialize empty lists
  lru_->next = ptr_to_idx(lru_);
  lru_->prev = ptr_to_idx(lru_);
  in_use_->next = ptr_to_idx(in_use_);
  in_use_->prev = ptr_to_idx(in_use_);

  // Put these entries into free list
  free_->next = 0;
  pool_[0].prev = ptr_to_idx(free_);
  free_->prev = capacity - 1;
  pool_[capacity - 1].next = ptr_to_idx(free_);
  for (size_t i = 0; i < capacity - 1; ++i) {
    pool_[i].next = i + 1;
    pool_[i + 1].prev = i;
  }
  table_.reserve(capacity, pool_);
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t* LRUCache<Key_t, Value_t>::insert(
    Key_t key, uint32_t hash, bool pin) {
  // Disable support for capacity_ == 0; the user must set capacity first
  assert(capacity_ > 0 && pool_);

  // Search to see if already exists
  Handle_t* e = table_.lookup(key, hash);
  if (e) {
    if (pin)
      ref(e);
    else if (e->refs == 1)
      lru_refresh(e);
    return e;
  }

  e = alloc_handle();
  if (!e) return nullptr;
  e->init(key, hash);
  table_.insert(e);
  assert(e->refs == 1);
  if (pin) {
    e->refs++;
    list_append(in_use_, e);
  } else {
    list_append(lru_, e);
  }
  return e;
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t* LRUCache<Key_t, Value_t>::lookup(
    Key_t key, uint32_t hash, bool pin) {
  Handle_t* e = table_.lookup(key, hash);
  if (e && pin) ref(e);
  return e;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::release(Handle_t* handle) {
  unref(handle);
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t* LRUCache<Key_t, Value_t>::touch(
    Key_t key, uint32_t hash, Handle_t*& successor) {
  // Disable support for capacity_ == 0; the user must set capacity first
  assert(capacity_ > 0 && pool_);

  // Search to see if already exists
  Handle_t* e = table_.lookup(key, hash);
  if (e) {
    successor = lru_refresh(e);
    return e;
  }

  successor = nullptr;
  e = alloc_handle();
  if (!e) return nullptr;
  e->init(key, hash);
  table_.insert(e);
  assert(e->refs == 1);
  list_append(lru_, e);
  return e;
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t*
LRUCache<Key_t, Value_t>::alloc_handle() {
  Handle_t* e = get_list_next(free_);
  if (e != free_) {  // Allocate from free list
    list_remove(e);
    return e;
  }

  // Evict one handle from LRU and recycle it
  e = get_list_next(lru_);
  if (e == lru_) return nullptr;  // No more space
  assert(e->refs == 1);
  list_remove(e);  // Remove from lru_
  [[maybe_unused]] Handle_t* e_;
  e_ = table_.remove(e->key, e->hash);
  assert(e_ == e);
  return e;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::free_handle(Handle_t* e) {
  list_append(free_, e);
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::ref(Handle_t* e) {
  if (e->refs == 1) {  // If on lru_ list, move to in_use_ list.
    list_remove(e);
    list_append(in_use_, e);
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
    list_append(lru_, e);
  }
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::list_remove(Handle_t* e) {
  get_list_next(e)->prev = e->prev;
  get_list_prev(e)->next = e->next;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::list_append(Handle_t* list, Handle_t* e) {
  // Make "e" newest entry by inserting just before *list
  e->next = ptr_to_idx(list);
  e->prev = list->prev;
  get_list_prev(e)->next = ptr_to_idx(e);
  get_list_next(e)->prev = ptr_to_idx(e);
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t*
LRUCache<Key_t, Value_t>::lru_refresh(Handle_t* e) {
  assert(e != lru_);
  assert(e->refs == 1);
  auto successor = get_list_next(e);
  if (successor == lru_) return e;  // no need to move
  list_remove(e);
  list_append(lru_, e);
  return successor;
}

template <typename Key_t, typename Value_t>
std::ostream& LRUCache<Key_t, Value_t>::print_list(std::ostream& os,
                                                   const Handle_t* list) const {
  auto e = get_list_next(list);
  if (e == list) return os;
  os << e->key;
  assert(e == get_list_prev(get_list_next(e)));
  e = get_list_next(e);
  while (e != list) {
    os << ", " << e->key;
    assert(e == get_list_prev(get_list_next(e)));
    e = get_list_next(e);
  }
  return os;
}

template <typename Key_t, typename Value_t>
std::ostream& LRUCache<Key_t, Value_t>::print(std::ostream& os,
                                              int indent) const {
  os << "LRUCache (capacity=" << capacity_ << ") {\n";
  for (int i = 0; i < indent + 1; ++i) os << '\t';
  os << "lru:    [";
  print_list(os, lru_);
  os << "]\n";
  for (int i = 0; i < indent + 1; ++i) os << '\t';
  os << "in_use: [";
  print_list(os, in_use_);
  os << "]\n";
  for (int i = 0; i < indent + 1; ++i) os << '\t';
  table_.print(os, indent + 1);
  for (int i = 0; i < indent; ++i) os << '\t';
  os << "}\n";
  return os;
}

}  // namespace gcache
