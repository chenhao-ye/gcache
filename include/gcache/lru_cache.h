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
  typedef typename HandleTable<Key_t, Value_t>::Handle_t Handle_t;
  LRUCache();
  LRUCache(size_t capacity) : LRUCache() { init_pool(capacity); };
  ~LRUCache();
  LRUCache(const LRUCache&) = delete;
  LRUCache(LRUCache&&) = delete;
  LRUCache& operator=(const LRUCache&) = delete;
  LRUCache& operator=(LRUCache&&) = delete;

  Handle_t* insert(Key_t key, uint32_t hash);
  Handle_t* lookup(Key_t key, uint32_t hash);
  void release(Handle_t* handle);
  void erase(Key_t key, uint32_t hash);
  void init_pool(size_t capacity);

 private:
  Handle_t* alloc_handle();
  void free_handle(Handle_t* e);
  void list_remove(Handle_t* e);
  void list_append(Handle_t* list, Handle_t* e);
  void ref(Handle_t* e);
  void unref(Handle_t* e, bool in_cache = true);
  bool finish_erase(Handle_t* e);

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
  friend std::ostream& operator<<(std::ostream& os, const LRUCache& c) {
    size_t cnt = 0;
    auto h = c.free_.next;
    while (h != &c.free_) {
      cnt++;
      assert(h == h->next->prev);
      os << h << ',';
      h = h->next;
    }
    os << "LRUCache (capacity=" << c.capacity_ << ", #free=" << cnt << ") {\n";
    os << "\tlru:    [";
    h = c.lru_.next;
    while (h != &c.lru_) {
      os << h->key << ", ";
      assert(h == h->next->prev);
      h = h->next;
    }
    os << "]\n";
    os << "\tin_use: [";
    h = c.in_use_.next;
    while (h != &c.in_use_) {
      os << h->key << ", ";
      assert(h == h->next->prev);
      h = h->next;
    }
    os << "]\n";
    os << c.table_;
    os << "}\n";
    return os;
  }
};

template <typename Key_t, typename Value_t>
LRUCache<Key_t, Value_t>::LRUCache() : capacity_(0), pool_(nullptr) {
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

  Handle_t* e = alloc_handle();
  if (!e) return e;  // Fail to allocate a new handle
  e->init(key, hash);
  e->refs++;  // for the cache's reference.
  list_append(&in_use_, e);
  finish_erase(table_.insert(e));
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
void LRUCache<Key_t, Value_t>::erase(Key_t key, uint32_t hash) {
  finish_erase(table_.remove(key, hash));
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
void LRUCache<Key_t, Value_t>::unref(Handle_t* e, bool in_cache) {
  assert(e->refs > 0);
  e->refs--;
  if (e->refs == 0) {  // Deallocate.
    free_handle(e);
  } else if (e->refs == 1 && in_cache) {
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

// If e != nullptr, finish removing *e from the cache; it has already been
// removed from the hash table.  Return whether e != nullptr.
template <typename Key_t, typename Value_t>
bool LRUCache<Key_t, Value_t>::finish_erase(Handle_t* e) {
  if (!e) return false;
  list_remove(e);
  unref(e, false);
  return true;
}

}  // namespace gcache
