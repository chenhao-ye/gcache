/**
 * Credit: this code is originally modified from Google LevelDB.
 */
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "node.h"
#include "table.h"

namespace gcache {

template <typename Hash, typename Meta>
class GhostCache;

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
class SharedCache;

// Key_t should be lightweight that can be pass-by-value
// Value_t should be trivially copyable
template <typename Key_t, typename Value_t, typename Hash>
class LRUCache {
  /**
   * Note the values are initialized once and never destructed during the
   * lifecycle of LRUCache; if a LRU replacement happens, a reused handle will
   * carry the previous value instead of the reinitialized.
   * This design choice is not only for performance reasons but also associated
   * with the usage pattern of LRU cache. In a typically use case, the key field
   * is a block number and the value field is a pointer to the physical location
   * of the cache. During the initialization, each node is assigned with a
   * pointer; when a LRU replacement happens, the same physical page will be
   * used to store the data of another page, in which case the key field will be
   * the new block number, but the value field remains the same pointer. This is
   * a fundamentally different from a key-value map, where the value's lifecycle
   * is binded to the key.
   */

 public:
  using Node_t = LRUNode<Key_t, Value_t>;
  using Handle_t = LRUHandle<Key_t, Value_t>;

  LRUCache();
  ~LRUCache();
  LRUCache(const LRUCache&) = delete;
  LRUCache(LRUCache&&) = delete;
  LRUCache& operator=(const LRUCache&) = delete;
  LRUCache& operator=(LRUCache&&) = delete;
  void init(size_t capacity);
  template <typename Fn>
  void init(size_t capacity, Fn&& fn);

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }

  // For each item in the cache, call fn(key, handle)
  template <typename Fn>
  void for_each(Fn&& fn) const;

  // For each item in the LRU list, call fn(key, handle) in LRU order
  template <typename Fn>
  void for_each_lru(Fn&& fn) const;

  // For each item in the LRU list, call fn(key, handle) in MRU order
  template <typename Fn>
  void for_each_mru(Fn&& fn) const;

  // For each item in the in-use list, call fn(key, handle)
  template <typename Fn>
  void for_each_in_use(Fn&& fn) const;

  // Conditionally for_each APIs; if fn return false, stop iteration
  template <typename Fn>
  void for_each_until_lru(Fn&& fn) const;
  template <typename Fn>
  void for_each_until_mru(Fn&& fn) const;

  // Set pin to be true to pin the returned node so it won't be recycled by
  // LRU; a pinned node must be unpinned later by calling release().

  // Insert a node into cache with given key and hash if not exists; if does,
  // return the existing one; if it is known for sure that the key must not
  // exist, set `hint_nonexist` to true to skip a lookup.
  Handle_t insert(Key_t key, bool pin = false, bool hint_nonexist = false);
  // Search for a node; return nullptr if not exist. This op will refresh LRU.
  Handle_t lookup(Key_t key, bool pin = false);
  // Release pinned node returned by insert/lookup.
  void release(Handle_t handle);
  // Pin a node returned by insert/lookup.
  void pin(Handle_t handle);

  /**
   * The normal opeartions (`insert`/`lookup`/`release`) will only cause a node
   * to flow among the lru list, the in-use list, and the free list.
   * Only `erase` will force a node to jump out of circulation, and only
   * `install` may add a node. When a node is erased, its value is trashed
   * (considered as random garbage bits); this is semantically different from a
   * node in the free list, where the value is long-lived. If the caller get a
   * node from `install`, the value fields must be overwritten before any read.
   */

  // Erase the non-null handle from the lru list; return whether erasing
  // succeeds (fail if not in lru). The erased handle will not be reused by
  // `insert`, and the caller should save the value if necessary.
  bool erase(Handle_t handle);

  // Install a handle into the lru list. This action will not use free list or
  // lru list but allocate additional space or reused space from erased
  // handles. The caller should set the value immediately.
  Handle_t install(Key_t key);

 private:
  /****************************************************************************/
  /* Below are intrusive functions that should only be called by SharedCache  */
  /****************************************************************************/

  // Init handle pool and table from externally instantiated ones but not owned
  // them; the caller must free the pool and table after dtor.
  void init_from(Node_t* pool, NodeTable<Key_t, Value_t>* table,
                 size_t capacity);

  // Force this cache to return a node (i.e. a cache slot) back to caller;
  // will try to return from free list first; if not available, preempt from
  // lru_ instead; if still not available, return nullptr.
  Handle_t preempt();

  // Assign a new node to this LRUCache; will be put into free list (duel with
  // `preempt`).
  void assign(Handle_t handle);

  /****************************************************************************/
  /* Below are intrusive functions that should only be called by GhostCache   */
  /****************************************************************************/

  // Similar to insert but 1) the targeted node must be in LRU list and this
  // function never pins it, 2) return `successor`: the node with the same order
  // as the returned node after LRU operations (nullptr if newly inserted).
  Handle_t refresh(Key_t key, uint32_t hash, Handle_t& successor);

 private:
  /* some internal implementation APIs (used by other classes in gcache) */
  Node_t* insert_impl(Key_t key, uint32_t hash, bool pin, bool hint_nonexist);
  Node_t* lookup_impl(Key_t key, uint32_t hash, bool pin);
  Node_t* install_impl(Key_t key);
  // Helper function for lookup: 1) pin the node if asked; 2) refresh LRU if in
  // the LRU list
  void lookup_refresh(Node_t* node, bool pin);

  Node_t* alloc_node();
  void free_node(Node_t* e);
  void list_remove(Node_t* e);
  void list_append(Node_t* list, Node_t* e);
  void ref(Node_t* e);
  void unref(Node_t* e);
  // Perform LRU operation and return the handle with the same order in the list
  // after LRU (usually it's e->next)
  Node_t* lru_refresh(Node_t* e);

  // Number of Node inserted (i.e. in table_).
  size_t size_;

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
  NodeTable<Key_t, Value_t>* table_;

  // Dummy head of LRU list.
  // lru.prev is the newest entry, lru.next is the oldest entry.
  // Entries have refs==1.
  Node_t lru_;

  // Dummy head of in-use list.
  // Entries are in use by clients and have refs >= 2.
  Node_t in_use_;

  // Dummy head of free list.
  Node_t free_;

  /* `erased_` and `extra_pool_` are only used by `erase/install`. */

  // Dummy head of erased list.
  // Entries here are erased, and all fields are just random garbage.
  // This list is only maintained for memory efficiency purposes.
  Node_t erased_;

  // Pool for additionaly allocated handles.
  std::vector<Node_t*> extra_pool_;

  template <typename H, typename M>
  friend class GhostCache;

  template <typename T, typename K, typename V, typename H>
  friend class SharedCache;

 public:  // for debugging
  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const LRUCache& c) {
    return c.print(os);
  }
};

template <typename Key_t, typename Value_t, typename Hash>
inline LRUCache<Key_t, Value_t, Hash>::LRUCache()
    : size_(0), capacity_(0), pool_(nullptr), table_(nullptr) {
  // Make empty circular linked lists.
  lru_.next = &lru_;
  lru_.prev = &lru_;
  in_use_.next = &in_use_;
  in_use_.prev = &in_use_;
  erased_.next = &erased_;
  erased_.prev = &erased_;
  // free_ will be initialized when init() is called
}

template <typename Key_t, typename Value_t, typename Hash>
inline LRUCache<Key_t, Value_t, Hash>::~LRUCache() {
  /* Could be an error if caller has an unreleased node */
  // assert(in_use_.next == &in_use_);

  /**
   * if `pool_` is nullptr, then this instance is initialized from `init_from`
   * not `init`, which means it owns neither pool_ nor table_. In this case, all
   * nodes have been freed so must avoid accessing them
   */
  if (pool_) {
    /* Unnecessary for correctness, but kept to ease debugging */
    // for (const Node_t* e = lru_.next; e != &lru_; e = e->next)
    //   assert(e->refs == 1);  // Invariant of lru_ list.
    delete[] pool_;
    delete table_;
  }
  /* `extrac_pool_` is always owned by this instance. */
  for (auto e : extra_pool_) delete e;
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::init(size_t capacity) {
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
  table_ = new NodeTable<Key_t, Value_t>();
  table_->init(capacity);
}

template <typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Hash>::init(size_t capacity, Fn&& fn) {
  init(capacity);
  for (size_t i = 0; i < capacity; ++i) fn(&pool_[i]);
}

template <typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Hash>::for_each(Fn&& fn) const {
  for_each_lru(fn);
  for_each_in_use(fn);
}

template <typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Hash>::for_each_lru(Fn&& fn) const {
  for (auto h = lru_.next; h != &lru_; h = h->next) fn(h);
}

template <typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Hash>::for_each_mru(Fn&& fn) const {
  for (auto h = lru_.prev; h != &lru_; h = h->prev) fn(h);
}

template <typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Hash>::for_each_in_use(Fn&& fn) const {
  for (auto h = in_use_.next; h != &in_use_; h = h->next) fn(h);
}

template <typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Hash>::for_each_until_lru(Fn&& fn) const {
  for (auto h = lru_.next; h != &lru_; h = h->next) {
    if (!fn(h)) break;
  }
}

template <typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void LRUCache<Key_t, Value_t, Hash>::for_each_until_mru(Fn&& fn) const {
  for (auto h = lru_.prev; h != &lru_; h = h->prev)
    if (!fn(h)) break;
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::init_from(
    Node_t* pool, NodeTable<Key_t, Value_t>* table, size_t capacity) {
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

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Handle_t
LRUCache<Key_t, Value_t, Hash>::insert(Key_t key, bool pin,
                                       bool hint_nonexist) {
  return insert_impl(key, Hash{}(key), pin, hint_nonexist);
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Node_t*
LRUCache<Key_t, Value_t, Hash>::insert_impl(Key_t key, uint32_t hash, bool pin,
                                            bool hint_nonexist) {
  // Disable support for capacity_ == 0; the user must set capacity first
  assert(capacity_ > 0);

  // Search to see if already exists
  Node_t* e;
  if (!hint_nonexist) {  // if not sure whether the key exists, do lookup
    e = lookup_impl(key, hash, pin);  // lookup_impl will do LRU refresh
    if (e) return e;
  } else {
    assert(!table_->lookup(key, hash));  // check if hint is correct
  }

  e = alloc_node();
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
  ++size_;
  return e;
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Handle_t
LRUCache<Key_t, Value_t, Hash>::lookup(Key_t key, bool pin) {
  return lookup_impl(key, Hash{}(key), pin);
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Node_t*
LRUCache<Key_t, Value_t, Hash>::lookup_impl(Key_t key, uint32_t hash,
                                            bool pin) {
  Node_t* e = table_->lookup(key, hash);
  if (e) lookup_refresh(e, pin);
  return e;
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::release(Handle_t handle) {
  // release can only called if the caller has previously pinned the handle;
  // the handle thus must still have nonzero refs
  Node_t* e = handle.node;
  assert(e->refs > 1);
  unref(e);
  assert(e->refs > 0);
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::pin(Handle_t handle) {
  ref(handle.node);
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Handle_t
LRUCache<Key_t, Value_t, Hash>::preempt() {
  // In fact, it is just like allocate a handle but instead of using it
  // immediately, return it out to caller (i.e. SharedCache).
  // We keep this function independent from `alloc_node` to make it
  // semantically clear.
  Node_t* e = alloc_node();
  if (e) --capacity_;
  return e;
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::assign(Handle_t e) {
  ++capacity_;
  free_node(e.node);
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::lookup_refresh(Node_t* node,
                                                           bool pin) {
  if (pin)
    ref(node);
  else if (node->refs == 1)
    lru_refresh(node);
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Handle_t
LRUCache<Key_t, Value_t, Hash>::refresh(Key_t key, uint32_t hash,
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
  e = alloc_node();
  if (!e) return nullptr;
  e->init(key, hash);
  table_->insert(e);
  assert(e->refs == 1);
  list_append(&lru_, e);
  ++size_;
  return e;
}

template <typename Key_t, typename Value_t, typename Hash>
inline bool LRUCache<Key_t, Value_t, Hash>::erase(Handle_t handle) {
  Node_t* e = handle.node;
  assert(e);
  if (e->refs != 1) return false;
  list_remove(e);
  list_append(&erased_, e);
  // it's actually fine to not decrement refs because later `Node_t::init` will
  // reset it. however, decrement it can help to detect "double-erase" issue.
  --e->refs;
  [[maybe_unused]] Node_t* e_;
  e_ = table_->remove(e->key, e->hash);
  assert(e_ == e);
  --size_;
  --capacity_;
  return true;
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Handle_t
LRUCache<Key_t, Value_t, Hash>::install(Key_t key) {
  return install_impl(key);
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Node_t*
LRUCache<Key_t, Value_t, Hash>::install_impl(Key_t key) {
  Node_t* e;
  if (erased_.next == &erased_) {
    e = new Node_t;  // caller is responsible for setting the value
    extra_pool_.emplace_back(e);
  } else {
    e = erased_.next;
    list_remove(e);
  }
  e->init(key, Hash{}(key));
  table_->insert(e);
  list_append(&lru_, e);
  ++size_;
  ++capacity_;
  return e;
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Node_t*
LRUCache<Key_t, Value_t, Hash>::alloc_node() {
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
  --size_;
  return e;
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::free_node(Node_t* e) {
  list_append(&free_, e);
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::ref(Node_t* e) {
  if (e->refs == 1) {  // If on lru_ list, move to in_use_ list.
    list_remove(e);
    list_append(&in_use_, e);
  }
  e->refs++;
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::unref(Node_t* e) {
  assert(e->refs > 0);
  e->refs--;
  if (e->refs == 0) {  // Deallocate.
    free_node(e);
  } else if (e->refs == 1) {
    // No longer in use; move to lru_ list.
    list_remove(e);
    list_append(&lru_, e);
  }
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::list_remove(Node_t* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

template <typename Key_t, typename Value_t, typename Hash>
inline void LRUCache<Key_t, Value_t, Hash>::list_append(Node_t* list,
                                                        Node_t* e) {
  // Make "e" newest entry by inserting just before *list
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

template <typename Key_t, typename Value_t, typename Hash>
inline typename LRUCache<Key_t, Value_t, Hash>::Node_t*
LRUCache<Key_t, Value_t, Hash>::lru_refresh(Node_t* e) {
  assert(e != &lru_);
  assert(e->refs == 1);
  auto successor = e->next;
  if (successor == &lru_) return e;  // no need to move
  list_remove(e);
  list_append(&lru_, e);
  return successor;
}

template <typename Key_t, typename Value_t, typename Hash>
inline std::ostream& LRUCache<Key_t, Value_t, Hash>::print(std::ostream& os,
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
