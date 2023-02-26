# gcache

gcache is a high-performance header-only LRU cache library. It provides not only a customizable cache implementation, but also support advanced features like ghost cache and multitenant cache.

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
#include <gcache/lru_cache.h>
#include <gcache/hash.h>  // provide useful hash function

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

Ghost cache is a type of cache maintained to answer the question "what will be cache hit rate if the cache size is X." It maintains the metadata of each cache slot without actual cache space.

```C++
#include <gcache/ghost_cache.h>
#include <gcache/hash.h>  // provide useful hash function

// ctor needs the spectrum of cache sizes: the example below will maintain hit
// rates for size=[4, 6, 8]
gcache::GhostCache ghost_cache(/*tick*/ 2, /*min_size*/ 4, /*max_size*/ 8);

// preheat: fill the cache
for (auto blk_id : {1, 2, 3, 4, 5, 6, 7, 8}) ghost_cache.access(blk_id);

// don't count the preheat ops towards hit rate status
ghost_cache.reset_stat();

// now run a trace of block accesses
for (auto blk_id : {8, 5, 3, 2, 2, 1, 9, 10}) ghost_cache.access(blk_id);

// show the hit rates
for (uint32_t cache_size = 4; cache_size <= 8; cache_size += 2)
  std::cout << "hit rate for size " << cache_size << ": "
            << ghost_cache.get_hit_rate(cache_size) << std::endl;
// expect size -> hit rate: {4: 0.375, 6: 0.5, 8: 0.75}
```

### Sampled Ghost Cache

Although ghost cache only maintains metadata of each cache slot, it could still be expensive to maintain both in terms of computation and memory. A good alternative is to use sampling. `SampledGhostCache` only samples a subspace of blocks. With proper sample rate, it could produce a decent approximation.

Using `SampledGhostCache` is straightforward. It is similar to `GhostCache` with an additional type parameter `SampleShift`, which indicates a sample rate of `1 / (1 << SampleShift)`. The default `SampleShift` is 5 (i.e., 1/32 sample rate).

```C++
// to make sure sampling is reasonable, `tick` must be no smaller than sample
// rate, and `min_size` and `max_size` must be multiple of sample rate.
gcache::SampledGhostCache<5> ghost_cache(/*tick*/ 64, /*min_size*/ 128, /*max_size*/ 640);
// or use `gcache::SampledGhostCache<>` to use default SampleShift=5
```

### Shared Cache

## Credits

gcache uses a modified version of the LRU page cache from Google's [LevelDB](https://github.com/google/leveldb).
