/**
 * Credit: this code is ported from Google LevelDB.
 */
#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>

namespace gcache {
// LRU cache implementation
//
// Cache entries have an "in_cache" boolean indicating whether the cache has a
// reference on the entry.  The only ways that this can become false without the
// entry being passed to its "deleter" are via erase(), via insert() when
// an element with a duplicate key is inserted, or on destruction of the cache.
//
// The cache keeps two linked lists of items in the cache.  All items in the
// cache are in one list or the other, and never both.  Items still referenced
// by clients but erased from the cache are in neither list.  The lists are:
// - in-use:  contains the items currently referenced by clients, in no
//   particular order.  (This list is used for invariant checking.  If we
//   removed the check, elements that would otherwise be on this list could be
//   left as disconnected singleton lists.)
// - LRU:  contains the items not currently referenced by clients, in LRU order
// Elements are moved between these lists by the Ref() and Unref() methods,
// when they detect an element in the cache acquiring or losing its only
// external reference.

template <typename Key_t, typename Value_t>
class NodeTable;

template <typename Key_t, typename Value_t, typename Hash>
class LRUCache;

template <typename Hash, typename Meta>
class GhostCache;

// LRUNodes forms a circular doubly linked list ordered by access time.
template <typename Key_t, typename Value_t>
class LRUNode {
  LRUNode *next_hash;
  LRUNode *next;
  LRUNode *prev;
  uint32_t refs;  // References, including cache reference, if present.

 protected:
  friend class NodeTable<Key_t, Value_t>;

  template <typename K, typename V, typename H>
  friend class LRUCache;

  template <typename H, typename M>
  friend class GhostCache;

 public:
  uint32_t hash;  // Hash of key; used for fast sharding and comparisons
  Key_t key;
  Value_t value;

  void init(Key_t k, uint32_t h) {
    this->refs = 1;
    this->hash = h;
    this->key = k;
  }

  // print for debugging
  friend std::ostream &operator<<(std::ostream &os, const LRUNode &h) {
    // value may not be printable...
    return os << h.key << " (refs=" << h.refs << ", hash=" << h.hash << ")";
  }

  // print a list; this must be a dummy list head
  std::ostream &print_list(std::ostream &os) const {
    auto h = next;
    if (h == this) return os;
    os << h->key;
    assert(h == h->next->prev);
    h = h->next;
    while (h != this) {
      os << ", " << h->key;
      assert(h == h->next->prev);
      h = h->next;
    }
    return os;
  }

  std::ostream &print_list_hash(std::ostream &os) const {
    auto h = this;
    while (h) {
      os << '\t' << *h << ';';
      h = h->next_hash;
    }
    os << '\n';
    return os;
  }
};

// BaseHandle implements the basic functional of a pointer-like data type
template <typename Node_t>
class BaseHandle {
 protected:
  Node_t *node = nullptr;

 public:
  BaseHandle(Node_t *node) : node(node) {}
  BaseHandle() = default;
  BaseHandle(const BaseHandle &) = default;
  BaseHandle(BaseHandle &&) noexcept = default;
  BaseHandle &operator=(const BaseHandle &) = default;
  BaseHandle &operator=(BaseHandle &&) noexcept = default;

  // overload bool
  explicit operator bool() const { return node != nullptr; }

  // overload == and !=
  bool operator==(std::nullptr_t) const { return node == nullptr; }
  bool operator!=(std::nullptr_t) const { return node != nullptr; }
  bool operator==(const BaseHandle &other) const { return node == other.node; }
  bool operator!=(const BaseHandle &other) const { return node != other.node; }
  // explicitly disallow comparison with raw Node_t pointer; users should not
  // have access to Node_t anyway; if other friend class needs to compare Handle
  // with Node_t pointer, must get node out explicitly
  bool operator==(const Node_t *&other) = delete;
  bool operator!=(const Node_t *&other) = delete;
};

// LRUHandle is essentially just LRUNode*.
// Use LRUHandle for public APIs as it's easier to access the value.
// Use LRUNode* for internal APIs as it's easier to access internal fields.
template <typename Key_t, typename Value_t>
class LRUHandle : public BaseHandle<LRUNode<Key_t, Value_t>> {
 private:
  using Node_t = LRUNode<Key_t, Value_t>;
  using BaseHandle<Node_t>::node;  // otherwise `node` will be invisible

 protected:
  friend class NodeTable<Key_t, Value_t>;

  template <typename K, typename V, typename H>
  friend class LRUCache;

  template <typename H, typename M>
  friend class GhostCache;

 public:
  LRUHandle(Node_t *node) : BaseHandle<Node_t>(node) {}
  LRUHandle() = default;
  LRUHandle(const LRUHandle &) = default;
  LRUHandle(LRUHandle &&) noexcept = default;
  LRUHandle &operator=(const LRUHandle &) = default;
  LRUHandle &operator=(LRUHandle &&) noexcept = default;

  // overload -> and * to use LRUHandle like Value_t*
  Value_t *operator->() { return &node->value; }
  const Value_t *operator->() const { return &node->value; }
  Value_t &operator*() { return node->value; }
  const Value_t &operator*() const { return &node->value; }

  Key_t get_key() const { return node->key; }
};

static_assert(sizeof(LRUHandle<int, int>) == 8);

}  // namespace gcache
