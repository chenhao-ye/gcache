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
  struct TaggedValue_t {
    Tag_t tag;
    Value_t value;
  };

 public:
  typedef LRUHandle<Key_t, TaggedValue_t> Handle_t;
  SharedCache();
  ~SharedCache();
  SharedCache(const SharedCache&) = delete;
  SharedCache(SharedCache&&) = delete;
  SharedCache& operator=(const SharedCache&) = delete;
  SharedCache& operator=(SharedCache&&) = delete;

  void init(const std::vector<std::pair<Tag_t, size_t>>& tenant_configs);

  // Insert a handle into cache with giveb key and hash if not exists; if does,
  // return the existing one
  Handle_t* insert(Tag_t tag, Key_t key, uint32_t hash, bool pin = false);
  // Search for a handle; return nullptr if not exist
  Handle_t* lookup(Tag_t tag, Key_t key, uint32_t hash, bool pin = false);
  // Release pinned handle returned by insert/lookup
  void release(Handle_t* handle);
  // Similar to insert but 1) never pin and the targeted handle must be in LRU
  // list, 2) return `successor`: the handle with the same order as the returned
  // handle after LRU operations (nullptr if newly inserted).
  Handle_t* touch(Tag_t tag, Key_t key, uint32_t hash, Handle_t*& successor);

  // Relocate some handles (i.e. cache slots) from src to dst
  size_t relocate(Tag_t src, Tag_t dst, size_t size);

 private:
  // Map each tenant's tag to its own cache
  std::unordered_map<Tag_t, LRUCache<Key_t, TaggedValue_t>*> tenant_cache_map;
  // Map each key to the cache that holds the key
  std::unordered_map<Key_t, LRUCache<Key_t, TaggedValue_t>*> key_cache_map;
};

}  // namespace gcache
