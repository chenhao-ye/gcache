#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "lru_cache.h"
#include "node.h"

namespace gcache {

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
class SharedCache;

template <typename Tag_t, typename Value_t>
struct TaggedValue {
  Tag_t tag;
  Value_t value;
};

template <typename Tag_t, typename Key_t, typename Value_t>
class TaggedHandle
    : public BaseHandle<LRUNode<Key_t, TaggedValue<Tag_t, Value_t>>> {
 private:
  using TaggedValue_t = TaggedValue<Tag_t, Value_t>;
  using Node_t = LRUNode<Key_t, TaggedValue_t>;
  using BaseHandle<Node_t>::node;  // otherwise `node` will be invisible

 public:
  TaggedHandle(Node_t* node) : BaseHandle<Node_t>(node) {}
  TaggedHandle() = default;
  TaggedHandle(const TaggedHandle&) = default;
  TaggedHandle(TaggedHandle&&) noexcept = default;
  TaggedHandle& operator=(const TaggedHandle&) = default;
  TaggedHandle& operator=(TaggedHandle&&) noexcept = default;

  // disallow directly converted from LRUHandle
  TaggedHandle(LRUHandle<Key_t, TaggedValue_t>) = delete;
  TaggedHandle& operator=(const LRUHandle<Key_t, TaggedValue_t>&) = delete;
  TaggedHandle& operator=(LRUHandle<Key_t, TaggedValue_t>&&) = delete;

  // multiplexing: node->value is of type `TaggedValue`; to get the real value,
  // must further access .value
  Value_t* operator->() { return &node->value.value; }
  const Value_t* operator->() const { return &node->value.value; }
  Value_t& operator*() { return node->value.value; }
  const Value_t& operator*() const { return &node->value.value; }

  Key_t get_key() const { return node->key; }
  Tag_t get_tag() const { return node->value.tag; }

 protected:
  void set_tag(Tag_t tag) { node->value.tag = tag; }

  // only visible to SharedCache: converted into LRUHandle
  LRUHandle<Key_t, TaggedValue_t> untagged() { return node; }

  template <typename T, typename K, typename V, typename H>
  friend class SharedCache;
};

// Each tenant should have a "tag" which uniquely identifies this tenant. Tag
// should be a lightweight type to copy.
template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
class SharedCache {
 private:
  using TaggedValue_t = TaggedValue<Tag_t, Value_t>;
  using Node_t = LRUNode<Key_t, TaggedValue_t>;

 public:
  using Handle_t = TaggedHandle<Tag_t, Key_t, Value_t>;
  using LRUCache_t = LRUCache<Key_t, TaggedValue_t, Hash>;

  SharedCache() : pool_(nullptr), table_(), tenant_cache_map_(){};
  ~SharedCache() { delete[] pool_; };
  SharedCache(const SharedCache&) = delete;
  SharedCache(SharedCache&&) = delete;
  SharedCache& operator=(const SharedCache&) = delete;
  SharedCache& operator=(SharedCache&&) = delete;

  void init(const std::vector<std::pair<Tag_t, size_t>>& tenant_configs);
  template <typename Fn>
  void init(const std::vector<std::pair<Tag_t, size_t>>& tenant_configs,
            Fn&& fn);

  size_t capacity() const { return total_capacity_; }
  // Note no `size()` is provided here, because it is unclear how useful to know
  // the overall size instead of each individual LRU cache's size, and it is
  // more complicated to maintain

  // Return the current cache capacity associated with the given tag
  size_t capacity_of(Tag_t tag) const;
  // Return the current cache size associated with the given tag
  size_t size_of(Tag_t tag) const;

  // For each item in the cache, call fn(key, handle)
  template <typename Fn>
  void for_each(Fn&& fn);

  // For all APIs taking a tag as input, the tag must be valid (i.e., exists in
  // tenant_configs); the behavior is undefined if not.

  // Insert a handle into cache with given key and hash if not exists; if does,
  // return the existing one
  Handle_t insert(Tag_t tag, Key_t key, bool pin = false,
                  bool hint_nonexist = false);
  // Search for a handle; return nullptr if not exist; no tag required because
  // there is no insertion may happen
  // FIXME: However, this op will refresh LRU list, so a tenant A could
  // repeatedly access a cache slot previously accessed by B and keep this slot
  // in memory, even though B does not use it anymore
  Handle_t lookup(Key_t key, bool pin = false);
  // Release pinned handle returned by insert/lookup
  void release(Handle_t handle);
  // Pin a handle returned by insert/lookup
  void pin(Handle_t handle);
  // `touch` is not implemented yet because it is mostly used on GhostCache and
  // it is unclear whether it is useful in the real cache

  // Relocate some handles (i.e. cache slots) from src to dst; the relocation
  // may be terminated early if src does not have enough available handles to
  // return; return number of handles relocated successfully
  size_t relocate(Tag_t src, Tag_t dst, size_t size);

  // Similar to LRUCache erase/install
  bool erase(Handle_t handle);
  Handle_t install(Tag_t tag, Key_t key);

  // Return a read-only access to the LRU cache associated with the tag
  const LRUCache_t& get_cache(Tag_t tag) const;

 private:
  Node_t* lookup_impl(Key_t key, uint32_t hash, bool pin);

  LRUCache_t& get_cache_mutable(Tag_t tag);

  Node_t* pool_;
  size_t total_capacity_;
  NodeTable<Key_t, TaggedValue_t> table_;

  // Map each tenant's tag to its own cache; must be const after `init`
  std::unordered_map<Tag_t, LRUCache_t> tenant_cache_map_;

 public:  // for debugging
  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const SharedCache& c) {
    return c.print(os);
  }
};

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
void SharedCache<Tag_t, Key_t, Value_t, Hash>::init(
    const std::vector<std::pair<Tag_t, size_t>>& tenant_configs) {
  total_capacity_ = 0;
  size_t begin_idx = 0;
  for (auto [tag, capacity] : tenant_configs) total_capacity_ += capacity;

  table_.init(total_capacity_);
  pool_ = new Node_t[total_capacity_];
  for (auto [tag, capacity] : tenant_configs) {
    auto [it, is_emplaced] = tenant_cache_map_.emplace(
        std::piecewise_construct, std::forward_as_tuple(tag),
        std::forward_as_tuple());
    assert(is_emplaced);
    it->second.init_from(&pool_[begin_idx], &table_, capacity);
    begin_idx += capacity;
  }
  assert(begin_idx == total_capacity_);
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void SharedCache<Tag_t, Key_t, Value_t, Hash>::init(
    const std::vector<std::pair<Tag_t, size_t>>& tenant_configs, Fn&& fn) {
  init(tenant_configs);
  for (size_t i = 0; i < total_capacity_; ++i) {
    fn(&pool_[i]);
  }
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
size_t SharedCache<Tag_t, Key_t, Value_t, Hash>::capacity_of(Tag_t tag) const {
  assert(tenant_cache_map_.contains(tag));
  return get_cache(tag).capacity();
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
size_t SharedCache<Tag_t, Key_t, Value_t, Hash>::size_of(Tag_t tag) const {
  assert(tenant_cache_map_.contains(tag));
  return get_cache(tag).size();
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
template <typename Fn>
inline void SharedCache<Tag_t, Key_t, Value_t, Hash>::for_each(Fn&& fn) {
  for (auto& [tag, cache] : tenant_cache_map_) {
    cache.for_each(fn);
  }
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline typename SharedCache<Tag_t, Key_t, Value_t, Hash>::Handle_t
SharedCache<Tag_t, Key_t, Value_t, Hash>::insert(Tag_t tag, Key_t key, bool pin,
                                                 bool hint_nonexist) {
  uint32_t hash = Hash{}(key);
  assert(tenant_cache_map_.contains(tag));

  Node_t* e;
  if (!hint_nonexist) {
    e = lookup_impl(key, hash, pin);
    if (e) return e;
  } else {
    assert(!table_.lookup(key, hash));
  }

  // The key does not exist in the cache, perform insertion
  e = get_cache_mutable(tag).insert_impl(key, hash, pin, /*not_exist*/ true);
  if (!e) return nullptr;
  Handle_t h(e);
  h.set_tag(tag);
  return h;
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline typename SharedCache<Tag_t, Key_t, Value_t, Hash>::Handle_t
SharedCache<Tag_t, Key_t, Value_t, Hash>::lookup(Key_t key, bool pin) {
  return lookup_impl(key, Hash{}(key), pin);
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline typename SharedCache<Tag_t, Key_t, Value_t, Hash>::Node_t*
SharedCache<Tag_t, Key_t, Value_t, Hash>::lookup_impl(Key_t key, uint32_t hash,
                                                      bool pin) {
  Node_t* e = table_.lookup(key, hash);
  if (!e) return nullptr;

  Tag_t tag = Handle_t(e).get_tag();
  assert(tenant_cache_map_.contains(tag));
  get_cache_mutable(tag).lookup_refresh(e, pin);
  return e;
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline void SharedCache<Tag_t, Key_t, Value_t, Hash>::release(Handle_t handle) {
  Tag_t tag = handle.get_tag();
  assert(tenant_cache_map_.contains(tag));
  get_cache_mutable(tag).release(handle.untagged());
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline void SharedCache<Tag_t, Key_t, Value_t, Hash>::pin(
    typename SharedCache<Tag_t, Key_t, Value_t, Hash>::Handle_t handle) {
  Tag_t tag = handle.get_tag();
  assert(tenant_cache_map_.contains(tag));
  get_cache_mutable(tag).pin(handle.untagged());
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline size_t SharedCache<Tag_t, Key_t, Value_t, Hash>::relocate(Tag_t src,
                                                                 Tag_t dst,
                                                                 size_t size) {
  assert(tenant_cache_map_.contains(src));
  assert(tenant_cache_map_.contains(dst));

  auto& src_cache = tenant_cache_map_[src];
  auto& dst_cache = tenant_cache_map_[dst];
  size_t n = 0;
  for (; n < size; ++n) {
    auto e = src_cache.preempt();
    if (!e) break;
    dst_cache.assign(e);
  }
  return n;
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline bool SharedCache<Tag_t, Key_t, Value_t, Hash>::erase(Handle_t handle) {
  assert(tenant_cache_map_.contains(handle.get_tag()));
  bool is_erased = tenant_cache_map_[handle.get_tag()].erase(handle.untagged());
  if (is_erased) --total_capacity_;
  return is_erased;
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline typename SharedCache<Tag_t, Key_t, Value_t, Hash>::Handle_t
SharedCache<Tag_t, Key_t, Value_t, Hash>::install(Tag_t tag, Key_t key) {
  assert(tenant_cache_map_.contains(tag));
  Node_t* e = get_cache_mutable(tag).install_impl(key);
  Handle_t h(e);
  h.set_tag(tag);
  ++total_capacity_;
  return h;
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline const typename SharedCache<Tag_t, Key_t, Value_t, Hash>::LRUCache_t&
SharedCache<Tag_t, Key_t, Value_t, Hash>::get_cache(Tag_t tag) const {
  assert(tenant_cache_map_.contains(tag));
  return tenant_cache_map_.find(tag)->second;
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline typename SharedCache<Tag_t, Key_t, Value_t, Hash>::LRUCache_t&
SharedCache<Tag_t, Key_t, Value_t, Hash>::get_cache_mutable(Tag_t tag) {
  assert(tenant_cache_map_.contains(tag));
  return tenant_cache_map_.find(tag)->second;
}

template <typename Tag_t, typename Key_t, typename Value_t, typename Hash>
inline std::ostream& SharedCache<Tag_t, Key_t, Value_t, Hash>::print(
    std::ostream& os, int indent) const {
  os << "Tenant Cache Map {" << std::endl;
  for (auto& [tag, cache] : tenant_cache_map_) {
    for (int i = 0; i < indent + 1; ++i) os << '\t';
    os << "Tenant (tag=" << tag << ") {\n";
    for (int i = 0; i < indent + 2; ++i) os << '\t';
    cache.print(os, indent + 2);
    for (int i = 0; i < indent + 1; ++i) os << '\t';
    os << "}\n";
  }
  for (int i = 0; i < indent; ++i) os << '\t';
  os << "}\n";
  return os;
}

}  // namespace gcache
