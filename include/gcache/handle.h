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

template <typename Node_t>
class HandleTable;
template <typename Key_t, typename Value_t, typename Tag_t>
class LRUCache;
class GhostCache;

struct EmptyTag {};

// LRUNodes forms a circular doubly linked list ordered by access time.
template <typename Key_t, typename Value_t, typename Tag_t = EmptyTag>
class LRUNode {
  LRUNode *next_hash;
  LRUNode *next;
  LRUNode *prev;
  uint32_t refs;  // References, including cache reference, if present.

  friend class HandleTable<LRUNode>;
  friend class LRUCache<Key_t, Value_t, Tag_t>;
  friend class GhostCache;

 public:
  // User-defined tag, [[no_unique_address]] is used so that the field does not
  // affect the size if it is EmptyTag.
  [[no_unique_address]] Tag_t tag;
  uint32_t hash;  // Hash of key; used for fast sharding and comparisons
  Key_t key;
  Value_t value;

  using key_type = Key_t;
  using value_type = Value_t;

  void init(Key_t k, uint32_t h) {
    this->refs = 1;
    this->hash = h;
    this->key = k;
  }

  // print for debugging
  friend std::ostream &operator<<(std::ostream &os, const LRUNode &h) {
    if constexpr (std::is_same_v<Tag_t, EmptyTag>) {
      return os << h.key << ": " << h.value << " (refs=" << h.refs
                << ", hash=" << h.hash << ")";
    } else {
      return os << h.key << ": " << h.value << " (refs=" << h.refs
                << ", hash=" << h.hash << ", tag=" << h.tag << ")";
    }
  }

  template <typename Fn>
  void for_each(Fn fn) {
    for (auto h = next; h != this; h = h->next) fn(h->key, h);
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

  template <typename Node_t>
  friend struct LRUHandle;
};

// make sure that Tag_t does not occupy space if it is not provided
static_assert(sizeof(LRUNode<int, int>) == 40);
static_assert(sizeof(LRUNode<int, int, int>) == 48);

// LRUHandle is essentially just LRUNode*.
// Use LRUHandle for public APIs as it's easier to access the value.
// Use LRUNode* for internal APIs as it's easier to access internal fields.
template <typename Node_t>
struct LRUHandle {
  LRUHandle() = default;
  LRUHandle(const LRUHandle &) = default;
  LRUHandle(LRUHandle &&) noexcept = default;
  LRUHandle &operator=(const LRUHandle &) = default;
  LRUHandle &operator=(LRUHandle &&) noexcept = default;

  LRUHandle(Node_t *node) : node(node) {}

  uint32_t get_refs() const { return node->refs; }

  // overload -> and *
  typename Node_t::value_type *operator->() { return &node->value; }
  const typename Node_t::value_type *operator->() const { return &node->value; }
  typename Node_t::value_type &operator*() { return node->value; }

  // overload bool
  explicit operator bool() const { return node != nullptr; }

  // overload == and !=
  bool operator==(std::nullptr_t) const { return node == nullptr; }
  bool operator!=(std::nullptr_t) const { return node != nullptr; }
  bool operator==(const LRUHandle &other) const { return node == other.node; }
  bool operator!=(const LRUHandle &other) const { return node != other.node; }
  // explicitly disallow comparison with raw Node_t pointer; users should not
  // have access to Node_t anyway; if other friend class needs to compare Handle
  // with Node_t pointer, must get node out explicitly
  bool operator==(const Node_t *&other) = delete;
  bool operator!=(const Node_t *&other) = delete;

  Node_t *node = nullptr;
};
static_assert(sizeof(LRUHandle<LRUNode<int, int>>) == 8);
}  // namespace gcache

namespace std {
template <typename Node_t>
struct hash<gcache::LRUHandle<Node_t>> {
  std::size_t operator()(const gcache::LRUHandle<Node_t> &h) const {
    return std::hash<void *>()(h.node);
  }
};
}  // namespace std
