/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>

#include "handle.h"
#include "handle_table.h"

namespace gcache {

// Key_t should be lightweight that can be passed by value
template <typename Key_t, typename Value_t>
class LRUCache {
 public:
  typedef typename HandleTable<Key_t, Value_t>::Handle_t Handle_t;
  LRUCache();
  ~LRUCache();
  LRUCache(const LRUCache&) = delete;
  LRUCache(LRUCache&&) = delete;
  LRUCache& operator=(const LRUCache&) = delete;
  LRUCache& operator=(LRUCache&&) = delete;

  // Insert a mapping from key->value into the cache and assign it
  // the specified charge against the total cache capacity.
  //
  // Returns a handle that corresponds to the mapping.  The caller
  // must call this->release(handle) when the returned mapping is no
  // longer needed.
  //
  // When the inserted entry is no longer needed, the key and
  // value will be passed to "deleter".
  Handle_t* insert(Key_t key, uint32_t hash);

  // If the cache has no mapping for "key", returns nullptr.
  //
  // Else return a handle that corresponds to the mapping.  The caller
  // must call this->release(handle) when the returned mapping is no
  // longer needed.
  Handle_t* lookup(Key_t key, uint32_t hash);

  // Release a mapping returned by a previous lookup().
  // REQUIRES: handle must not have been released yet.
  // REQUIRES: handle must have been returned by a method on *this.
  void release(Handle_t* handle);

  // If the cache contains entry for key, erase it.  Note that the
  // underlying entry will be kept around until all existing handles
  // to it have been released.
  void erase(Key_t key, uint32_t hash);

  // Separate from constructor so caller can easily make an array of LRUCache
  void set_capacity(size_t capacity) { capacity_ = capacity; }

 private:
  void lru_remove(Handle_t* e);
  void lru_append(Handle_t* list, Handle_t* e);
  void ref(Handle_t* e);
  void unref(Handle_t* e);
  bool finish_erase(Handle_t* e);

  // Initialized before use.
  size_t capacity_;
  size_t size_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  // Entries have refs==1 and in_cache==true.
  Handle_t lru_;

  // Dummy head of in-use list.
  // Entries are in use by clients, and have refs >= 2 and in_cache==true.
  Handle_t in_use_;

  HandleTable<Key_t, Value_t> table_;
};

template <typename Key_t, typename Value_t>
LRUCache<Key_t, Value_t>::LRUCache() : capacity_(0), size_(0) {
  // Make empty circular linked lists.
  lru_.next = &lru_;
  lru_.prev = &lru_;
  in_use_.next = &in_use_;
  in_use_.prev = &in_use_;
}

template <typename Key_t, typename Value_t>
LRUCache<Key_t, Value_t>::~LRUCache() {
  assert(in_use_.next == &in_use_);  // Error if caller has an unreleased handle
  for (Handle_t* e = lru_.next; e != &lru_;) {
    Handle_t* next = e->next;
    assert(e->in_cache);
    e->in_cache = false;
    assert(e->refs == 1);  // Invariant of lru_ list.
    unref(e);
    e = next;
  }
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::ref(Handle_t* e) {
  if (e->refs == 1 && e->in_cache) {  // If on lru_ list, move to in_use_ list.
    lru_remove(e);
    lru_append(&in_use_, e);
  }
  e->refs++;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::unref(Handle_t* e) {
  assert(e->refs > 0);
  e->refs--;
  if (e->refs == 0) {  // Deallocate.
    assert(!e->in_cache);
    delete e;
  } else if (e->in_cache && e->refs == 1) {
    // No longer in use; move to lru_ list.
    lru_remove(e);
    lru_append(&lru_, e);
  }
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::lru_remove(Handle_t* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::lru_append(Handle_t* list, Handle_t* e) {
  // Make "e" newest entry by inserting just before *list
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t* LRUCache<Key_t, Value_t>::lookup(
    Key_t key, uint32_t hash) {
  Handle_t* e = table_.lookup(key, hash);
  if (e != nullptr) {
    ref(e);
  }
  return e;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::release(Handle_t* handle) {
  unref(handle);
}

template <typename Key_t, typename Value_t>
typename LRUCache<Key_t, Value_t>::Handle_t* LRUCache<Key_t, Value_t>::insert(
    Key_t key, uint32_t hash) {
  Handle_t* e = new Handle_t;
  e->key = key;
  e->hash = hash;
  e->in_cache = false;
  e->refs = 1;

  if (capacity_ > 0) {
    e->refs++;  // for the cache's reference.
    e->in_cache = true;
    lru_append(&in_use_, e);
    size_++;
    finish_erase(table_.insert(e));
  } else {  // don't cache. (capacity_==0 is supported and turns off caching.)
    // next is read by key() in an assert, so it must be initialized
    e->next = nullptr;
  }
  while (size_ > capacity_ && lru_.next != &lru_) {
    Handle_t* old = lru_.next;
    assert(old->refs == 1);
    bool erased = finish_erase(table_.remove(old->key(), old->hash));
    if (!erased) {  // to avoid unused variable when compiled NDEBUG
      assert(erased);
    }
  }
  return e;
}

// If e != nullptr, finish removing *e from the cache; it has already been
// removed from the hash table.  Return whether e != nullptr.
template <typename Key_t, typename Value_t>
bool LRUCache<Key_t, Value_t>::finish_erase(Handle_t* e) {
  if (e != nullptr) {
    assert(e->in_cache);
    lru_remove(e);
    e->in_cache = false;
    size_--;
    unref(e);
  }
  return e != nullptr;
}

template <typename Key_t, typename Value_t>
void LRUCache<Key_t, Value_t>::erase(Key_t key, uint32_t hash) {
  finish_erase(table_.remove(key, hash));
}

}  // namespace gcache
