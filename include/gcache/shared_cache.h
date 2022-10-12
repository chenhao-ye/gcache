#include <cstddef>
#include <unordered_map>
#include <vector>

#include "gcache/handle.h"
#include "gcache/lru_cache.h"

namespace gcache {

// Each tenant should have a "tag" which uniquely identifies this tenant. Tag
// should be a lightweight type to copy.
template <typename Tag_t, typename Key_t, typename Value_t>
class SharedCache {
 public:
  struct TaggedValue_t {
    Tag_t tag;
    Value_t value;
    friend std::ostream& operator<<(std::ostream& os, const TaggedValue_t& tv) {
      return os << "(" << tv.tag << ", " << tv.value << ")";
    }
  };
  typedef LRUHandle<Key_t, TaggedValue_t> Handle_t;
  SharedCache() : pool_(nullptr), table_(), tenant_cache_map_(){};
  ~SharedCache() { delete[] pool_; };
  SharedCache(const SharedCache&) = delete;
  SharedCache(SharedCache&&) = delete;
  SharedCache& operator=(const SharedCache&) = delete;
  SharedCache& operator=(SharedCache&&) = delete;

  void init(const std::vector<std::pair<Tag_t, size_t>>& tenant_configs);

  // Insert a handle into cache with given key and hash if not exists; if does,
  // return the existing one
  Handle_t* insert(Tag_t tag, Key_t key, uint32_t hash, bool pin = false);
  // Search for a handle; return nullptr if not exist; no tag required because
  // there is no insertion may happen
  // FIXME: However, this op will refresh LRU list, so a tenant A could
  // repeatedly access a cache slot previously accessed by B and keep this slot
  // in memory, even though B does not use it anymore
  Handle_t* lookup(Key_t key, uint32_t hash, bool pin = false);
  // Release pinned handle returned by insert/lookup
  void release(Handle_t* handle);
  // `touch` is not implemented yet because it is mostly used on GhostCache and
  // it is unclear whether it is useful in the real cache

  // Relocate some handles (i.e. cache slots) from src to dst; the relocation
  // may be terminated early if dst does not have enough available handles to
  // return; return number of handles relocated successfully
  size_t relocate(Tag_t src, Tag_t dst, size_t size);

  // handle op wrappers
  static Tag_t get_tag(Handle_t* e) { return e->value.tag; }
  static Key_t get_key(Handle_t* e) { return e->value.key; }
  static Value_t& get_value(Handle_t* e) { return e->value.value; }

 private:
  Handle_t* pool_;
  HandleTable<Key_t, TaggedValue_t> table_;

  // Map each tenant's tag to its own cache; must be const after `init`
  std::unordered_map<Tag_t, LRUCache<Key_t, TaggedValue_t>> tenant_cache_map_;

 public:  // for debugging
  std::ostream& print(std::ostream& os, int indent = 0) const;
  friend std::ostream& operator<<(std::ostream& os, const SharedCache& c) {
    return c.print(os);
  }
};

template <typename Tag_t, typename Key_t, typename Value_t>
void SharedCache<Tag_t, Key_t, Value_t>::init(
    const std::vector<std::pair<Tag_t, size_t>>& tenant_configs) {
  size_t total_capacity = 0;
  size_t begin_idx = 0;
  for (auto [tag, capacity] : tenant_configs) total_capacity += capacity;

  table_.init(total_capacity);
  pool_ = new Handle_t[total_capacity];
  for (auto [tag, capacity] : tenant_configs) {
    tenant_cache_map_[tag].init_from(&pool_[begin_idx], &table_, capacity);
    begin_idx += capacity;
  }
  assert(begin_idx == total_capacity);
}

template <typename Tag_t, typename Key_t, typename Value_t>
inline typename SharedCache<Tag_t, Key_t, Value_t>::Handle_t*
SharedCache<Tag_t, Key_t, Value_t>::insert(Tag_t tag, Key_t key, uint32_t hash,
                                           bool pin) {
  assert(tenant_cache_map_.contains(tag));
  Handle_t* e = lookup(key, hash, pin);
  if (e) return e;

  // The key does not exist in the cache, perform insertion
  e = tenant_cache_map_[tag].insert(key, hash, pin, /*not_exist*/ true);
  if (!e) return nullptr;
  e->value.tag = tag;
  return e;
}

template <typename Tag_t, typename Key_t, typename Value_t>
inline typename SharedCache<Tag_t, Key_t, Value_t>::Handle_t*
SharedCache<Tag_t, Key_t, Value_t>::lookup(Key_t key, uint32_t hash, bool pin) {
  Handle_t* e = table_.lookup(key, hash);
  if (!e) return nullptr;

  Tag_t tag = get_tag(e);
  assert(tenant_cache_map_.contains(tag));
  tenant_cache_map_[tag].try_refresh(e, pin);
  return e;
}

template <typename Tag_t, typename Key_t, typename Value_t>
void SharedCache<Tag_t, Key_t, Value_t>::release(
    typename SharedCache<Tag_t, Key_t, Value_t>::Handle_t* handle) {
  Tag_t tag = handle->value.tag;
  assert(tenant_cache_map_.contains(tag));
  tenant_cache_map_[tag].release(handle);
}

template <typename Tag_t, typename Key_t, typename Value_t>
size_t SharedCache<Tag_t, Key_t, Value_t>::relocate(Tag_t src, Tag_t dst,
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

template <typename Tag_t, typename Key_t, typename Value_t>
std::ostream& SharedCache<Tag_t, Key_t, Value_t>::print(std::ostream& os,
                                                        int indent) const {
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
