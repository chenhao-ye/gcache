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

class GhostCache;

// Key_t should be lightweight that can be pass-by-value
// Value_t should be trivially copyable
template <typename Key_t, typename Value_t, typename Tag_t = EmptyTag>
class LRUCache {
 public:
  using Node_t = LRUNode<Key_t, Value_t, Tag_t>;
  using Handle_t = LRUHandle<Node_t>;
  LRUCache();
  ~LRUCache();
  LRUCache(const LRUCache&) = delete;
  LRUCache(LRUCache&&) = delete;
  LRUCache& operator=(const LRUCache&) = delete;
  LRUCache& operator=(LRUCache&&) = delete;
  void init(size_t capacity);
  template <typename Fn>
  void init(size_t capacity, Fn&& fn);

  // For each item in the cache, call fn(key, handle)
  template <typename Fn>
  void for_each(Fn&& fn);

  // Set pin to be true to pin the returned Handle so it won't be recycled by
  // LRU; a pinned handle must be unpinned later by calling release()

  // Insert a handle into cache with given key and hash if not exists; if does,
  // return the existing one; if it is known for sure that the key must not
  // exist, set not_exist to true to skip a lookup
  Handle_t insert(Key_t key, uint32_t hash, bool pin = false,
                  bool not_exist = false);
  // Search for a handle; return nullptr if not exist
  Handle_t lookup(Key_t key, uint32_t hash, bool pin = false);
  // Release pinned handle returned by insert/lookup
  void release(Handle_t handle);
  // Pin a handle returned by insert/lookup
  void pin(Handle_t handle);
  // Similar to insert but 1) never pin and the targeted handle must be in LRU
  // list, 2) return `successor`: the handle with the same order as the returned
  // handle after LRU operations (nullptr if newly inserted).
  Handle_t touch(Key_t key, uint32_t hash, Handle_t& successor);

  /****************************************************************************/
  /* Below are intrusive functions that should only be called by SharedCache  */
  /****************************************************************************/

  // Init handle pool and table from externally instantiated ones but not owned
  // them; the caller must free the pool and table after dtor
  void init_from(Node_t* pool, HandleTable<Node_t>* table, size_t capacity);

  // Force this cache to return a handle (i.e. a cache slot) back to caller;
  // will try to return from free list first; if not available, preempt from
  // lru_ instead; if still not available, return nullptr
  Handle_t preempt();

  // Assign a new handle to this LRUCache; will be put into free list (duel with
  // `preempt`)
  void assign(Handle_t e);

  void try_refresh(Handle_t e, bool pin);

 private:
  friend GhostCache;
  Node_t* alloc_handle();
  void free_handle(Node_t* e);
  void list_remove(Node_t* e);
  void list_append(Node_t* list, Node_t* e);
  void ref(Node_t* e);
  void unref(Node_t* e);
  // Perform LRU operation and return the handle with the same order in the list
  // after LRU (usually it's e->next)
  Node_t* lru_refresh(Node_t* e);

  // Initialized before use.
  size_t capacity_;

  // Manage batch of handle and place into the free list.
  // Allocate a handle from free_ and put it into table_; a handle in table_
  // must either present in lru_ or in_use_
  // If user calls `init_from`, this field will be nullptr
  Node_t* pool_;

  // Hash table to lookup
  // If user calls `init_from`, this field will just refer to the external one;
  // otherwise, managed by this class instance
  HandleTable<Node_t>* table_;

  // Dummy head of LRU list.
  // lru.prev is the newest entry, lru.next is the oldest entry.
  // Entries have refs==1.
  Node_t lru_;

  // Dummy head of in-use list.
  // Entries are in use by clients and have refs >= 2.
  Node_t in_use_;

  // Dummy head of free list.
  Node_t free_;

 public:  // for debugging
  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const LRUCache& c) {
    return c.print(os);
  }
};

template <typename Key_t, typename Value_t, typename Tag_t>
inline LRUCache<Key_t, Value_t, Tag_t>::LRUCache()
    : capacity_(0), pool_(nullptr), table_(nullptr) {
  // Make empty circular linked lists.
  lru_.next = &lru_;
  lru_.prev = &lru_;
  in_use_.next = &in_use_;
  in_use_.prev = &in_use_;
  // free_ will be initialized when init() is called
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline LRUCache<Key_t, Value_t, Tag_t>::~LRUCache() {
  assert(in_use_.next == &in_use_);  // Error if caller has an unreleased handle
  // if pool_ is nullptr, then this instance is initialized from `init_from` not
  // `init`, which means it owns neither pool_ nor table_. In this case, all
  // handles have been freed so must avoid accessing them
  if (pool_) {
    // if pool_ is
    // Unnecessary for correctness, but kept to ease debugging
    for (const Node_t* e = lru_.next; e != &lru_; e = e->next)
      assert(e->refs == 1);  // Invariant of lru_ list.
    delete[] pool_;
    delete table_;
  }
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline void LRUCache<Key_t, Value_t, Tag_t>::init(size_t capacity) {
  assert(!capacity_ && !pool_ && !table_);
  assert(capacity);
  capacity_ = capacity;
  pool_ = new Node_t[capacity];
  // Put these entries into free list
  free_.next = &pool_[0];
  pool_[0].prev = &free_;
  free_.prev = &pool_[capacity - 1];
  pool_[capacity - 1].next = &free_;
  for (size_t i = 0; i < capacity - 1; ++i) {
    pool_[i].next = &pool_[i + 1];
    pool_[i + 1].prev = &pool_[i];
  }
  table_ = new HandleTable<Node_t>();
  table_->init(capacity);
}

template <typename Key_t, typename Value_t, typename Tag_t>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Tag_t>::init(size_t capacity, Fn&& fn) {
  init(capacity);
  for (size_t i = 0; i < capacity; ++i) {
    fn(&pool_[i]);
  }
}

template <typename Key_t, typename Value_t, typename Tag_t>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Tag_t>::for_each(Fn&& fn) {
  in_use_.for_each(fn);
  lru_.for_each(fn);
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline void LRUCache<Key_t, Value_t, Tag_t>::init_from(
    Node_t* pool, HandleTable<Node_t>* table, size_t capacity) {
  assert(!capacity_ && !pool_ && !table_);
  assert(capacity);
  capacity_ = capacity;
  // same as `init` but directly use `pool` instead of `pool_`
  free_.next = &pool[0];
  pool[0].prev = &free_;
  free_.prev = &pool[capacity - 1];
  pool[capacity - 1].next = &free_;
  for (size_t i = 0; i < capacity - 1; ++i) {
    pool[i].next = &pool[i + 1];
    pool[i + 1].prev = &pool[i];
  }
  table_ = table;
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline typename LRUCache<Key_t, Value_t, Tag_t>::Handle_t
LRUCache<Key_t, Value_t, Tag_t>::insert(Key_t key, uint32_t hash, bool pin,
                                        bool not_exist) {
  // Disable support for capacity_ == 0; the user must set capacity first
  assert(capacity_ > 0);

  // Search to see if already exists
  Node_t* e;

  if (!not_exist) {  // if not sure whether the key exists, do lookup
    e = lookup(key, hash, pin).node;
    if (e) return e;
  }

  e = alloc_handle();
  if (!e) return nullptr;
  e->init(key, hash);
  table_->insert(e);
  assert(e->refs == 1);
  if (pin) {
    e->refs++;
    list_append(&in_use_, e);
  } else {
    list_append(&lru_, e);
  }
  return e;
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline typename LRUCache<Key_t, Value_t, Tag_t>::Handle_t
LRUCache<Key_t, Value_t, Tag_t>::lookup(Key_t key, uint32_t hash, bool pin) {
  Handle_t e = table_->lookup(key, hash);
  if (e) try_refresh(e, pin);
  return e;
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline void LRUCache<Key_t, Value_t, Tag_t>::release(Handle_t handle) {
  unref(handle.node);
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline void LRUCache<Key_t, Value_t, Tag_t>::pin(Handle_t handle) {
  ref(handle.node);
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline typename LRUCache<Key_t, Value_t, Tag_t>::Handle_t
LRUCache<Key_t, Value_t, Tag_t>::touch(Key_t key, uint32_t hash,
                                       Handle_t& successor) {
  // Disable support for capacity_ == 0; the user must set capacity first
  assert(capacity_ > 0);

  // Search to see if already exists
  Node_t* e = table_->lookup(key, hash);
  if (e) {
    successor = lru_refresh(e);
    return e;
  }

  successor = nullptr;
  e = alloc_handle();
  if (!e) return nullptr;
  e->init(key, hash);
  table_->insert(e);
  assert(e->refs == 1);
  list_append(&lru_, e);
  return e;
}

template <typename Key_t, typename Value_t, typename Tag_t>
typename LRUCache<Key_t, Value_t, Tag_t>::Handle_t
LRUCache<Key_t, Value_t, Tag_t>::preempt() {
  // In fact, it is just like allocate a handle but instead of using it
  // immediately, return it out to caller (i.e. SharedCache)
  // We keep this function independent from `alloc_handle` to make it
  // semantically clear
  return alloc_handle();
}

template <typename Key_t, typename Value_t, typename Tag_t>
void LRUCache<Key_t, Value_t, Tag_t>::assign(Handle_t e) {
  free_handle(e.node);
}

template <typename Key_t, typename Value_t, typename Tag_t>
void LRUCache<Key_t, Value_t, Tag_t>::try_refresh(Handle_t handle, bool pin) {
  Node_t* e = handle.node;
  assert(e);
  if (pin)
    ref(e);
  else if (e->refs == 1)
    lru_refresh(e);
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline typename LRUCache<Key_t, Value_t, Tag_t>::Node_t*
LRUCache<Key_t, Value_t, Tag_t>::alloc_handle() {
  if (free_.next != &free_) {  // Allocate from free list
    Node_t* e = free_.next;
    list_remove(e);
    return e;
  }

  // Evict one handle from LRU and recycle it
  if (lru_.next == &lru_) return nullptr;  // No more space
  Node_t* e = lru_.next;
  assert(e->refs == 1);
  list_remove(e);  // Remove from lru_
  [[maybe_unused]] Node_t* e_;
  e_ = table_->remove(e->key, e->hash);
  assert(e_ == e);
  return e;
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline void LRUCache<Key_t, Value_t, Tag_t>::free_handle(Node_t* e) {
  list_append(&free_, e);
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline void LRUCache<Key_t, Value_t, Tag_t>::ref(Node_t* e) {
  if (e->refs == 1) {  // If on lru_ list, move to in_use_ list.
    list_remove(e);
    list_append(&in_use_, e);
  }
  e->refs++;
}

template <typename Key_t, typename Value_t, typename Tag_t>
inline void LRUCache<Key_t, Value_t, Tag_t>::unref(Node_t* e) {
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

template <typename Key_t, typename Value_t, typename Tag_t>
void LRUCache<Key_t, Value_t, Tag_t>::list_remove(Node_t* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

template <typename Key_t, typename Value_t, typename Tag_t>
void LRUCache<Key_t, Value_t, Tag_t>::list_append(Node_t* list, Node_t* e) {
  // Make "e" newest entry by inserting just before *list
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

template <typename Key_t, typename Value_t, typename Tag_t>
typename LRUCache<Key_t, Value_t, Tag_t>::Node_t*
LRUCache<Key_t, Value_t, Tag_t>::lru_refresh(Node_t* e) {
  assert(e != &lru_);
  assert(e->refs == 1);
  auto successor = e->next;
  if (successor == &lru_) return e;  // no need to move
  list_remove(e);
  list_append(&lru_, e);
  return successor;
}

template <typename Key_t, typename Value_t, typename Tag_t>
std::ostream& LRUCache<Key_t, Value_t, Tag_t>::print(std::ostream& os,
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
  table_->print(os, indent + 1);
  for (int i = 0; i < indent; ++i) os << '\t';
  os << "}\n";
  return os;
}

}  // namespace gcache
