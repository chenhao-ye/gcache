# gcache

gcache is a high-performance header-only LRU cache library. It provides not only a customizable cache implementation, but also support advanced features like ghost cache, multitenant cache.

## Build

To use gcache, simply add this line to `CMakeLists.txt`:

```
include_directories(gcache/include)
```

To run tests and benchmarks of gcache:

```shell
mkdir build
cd build
cmake ..
make -j
```

## Usage

The major functionality of gcache is implemented as four classes:

- `LRUCache`: Basic implementation of a LRU Cache.

- `GhostCache`: A ghost cache is a metadata-only cache. It reports the hit rate for a spectrum of cache size. This is useful to tune cache size.

- `SampledGhostCache`: This is an augmented version of `GhostCache`. Instead of running over a full trace, it only samples a subspace. This could produce hit rate with decent accuracy, but with much less overhead.

- `SharedCache`: This is a multitenant cache: multiple tenants share a limited cache capacity. User could adjust each individual's cache size.

### LRU Cache

Below is an example of LRU cache usage. It allocate a page cache space (2 pages in this example). The key of the cache operation is the block number, and the value is a pointer to a page cache slot. For more advanced usage, one could use a struct that contains not only the pointer but additional metadata (e.g., whether the page is dirty).

```C++
char* page_cache = new char[4096 * 2];  // allocate a 2-page cache

using Cache_t = gcache::LRUCache</*Key_t*/ uint32_t, /*Value_t*/ char*,
                                  /*Hash*/ gcache::ghash>;
Cache_t lru_cache;
// init: 1) set cache capacity; 2) put page cache pointers into buffer
lru_cache.init(/*capacity*/ 2,
                /* init_func*/
                [&, i = 0l](Cache_t::Handle_t handle) mutable {
                  *handle = page_cache + (i++) * 4096;
                });
// alternatively, one could just use `insert`

// add block 1 & 2
auto h1 = lru_cache.insert(/*key: block_num*/ 1);  // return a handle
memcpy(/*buf*/ *h1, "This is block 1", 16);
auto h2 = lru_cache.insert(2);
memcpy(*h2, "This is block 2", 16);

// lookup block 1
h1 = lru_cache.lookup(1);
assert(memcmp(*h1, "This is block 1", 16) == 0);

// now add block 3 will trigger LRU replacement
auto h3 = lru_cache.insert(3);
// block 2 is evicted since we just access block 1; the buffer is recycled
assert(memcmp(*h3, "This is block 2", 16) == 0);
memcpy(*h3, "This is block 3", 16);
h2 = lru_cache.lookup(2);
assert(!h2);  // block 2 is not found

// one could pin a block to prevent it from being LRU replaced
auto h1_pinned = lru_cache.lookup(1, /*pin*/ true);
auto h4 = lru_cache.insert(4);
memcpy(*h4, "This is block 4", 16);

h1 = lru_cache.lookup(1);
assert(h1);  // block 1 still in the cache
h3 = lru_cache.lookup(3);
assert(!h3);  // but block 3 is replaced

// must release pinned block
lru_cache.release(h1_pinned);
```

### Ghost Cache

### Shared Cache

## Credits

gcache uses a modified version of the LRU page cache from Google's [LevelDB](https://github.com/google/leveldb).
