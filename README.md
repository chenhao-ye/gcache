# gcache
![Build & Test](https://github.com/chenhao-ye/gcache/actions/workflows/cmake.yml/badge.svg)

gcache is a high-performance header-only LRU cache library. It provides not only a customizable cache implementation, but also supports advanced features like ghost cache and multitenant cache.

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

The default build uses Intel SSE4.2 instruction set for the efficient CRC hash computation. If this is not compatible to your machine, you could modify `gcache::ghash` in `include/gcache/hash.h` to use other hash functions and remove `-msse4.2` from `CMakeLists.txt`.

## Usage

The major functionality of gcache is implemented as four classes:

- `LRUCache`: Basic implementation of a LRU Cache.

- `GhostCache`: A ghost cache is a metadata-only cache. It reports the hit rate for a spectrum of cache sizes. This is useful to tune cache size.

- `SampledGhostCache`: This is an augmented version of `GhostCache`. Instead of running over a full trace, it only samples a subspace. This could produce hit rates with decent accuracy, but with much less overhead.

- `SharedCache`: This is a multitenant cache: multiple tenants share a limited cache capacity. Users could adjust each individual's cache size.

### LRU Cache

Below is an example of LRU cache usage. It allocates a page cache space (2 pages in this example). The key of the cache operation is the block number, and the value is a pointer to a page cache slot. For more advanced usage, one could use a struct that contains not only the pointer but additional metadata (e.g., whether the page is dirty).

```C++
#include <gcache/hash.h>  // provide useful hash function
#include <gcache/lru_cache.h>

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

Ghost cache is a type of cache maintained to answer the question "what the cache hit rate will be if the cache size is X." It maintains the metadata of each cache slot without actual cache space.

```C++
#include <gcache/ghost_cache.h>

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

Although ghost cache only the maintains metadata of each cache slot, it could still be expensive to maintain both in terms of computation and memory. A good alternative is to use sampling. `SampledGhostCache` only samples a subspace of blocks. With a proper sample rate, it could produce a decent approximation.

Using `SampledGhostCache` is straightforward. It is similar to `GhostCache` with an additional type parameter `SampleShift`, which indicates a sample rate of `1 / (1 << SampleShift)`. The default `SampleShift` is 5 (i.e., 1/32 sample rate).

```C++
// to make sure sampling is reasonable, `tick` must be no smaller than the
// sample rate, and `min_size` and `max_size` must be multiple of sample rate.
gcache::SampledGhostCache</*SampleShift*/5> ghost_cache(
  /*tick*/ 64, /*min_size*/ 128, /*max_size*/ 640);
// or use `gcache::SampledGhostCache<>` to use default SampleShift=5
```

Using sampling not only reduces the computation and memory cost when playing the block access trace but also significantly reduces the footprint on the CPU cache. As a result, the end throughput improvement might be much higher than `1 << SampleShift`.

### Shared Cache

`SharedCache` is a more advanced version of LRU cache: multitenant cache. This is designed for a scenario where:

1. there is a fixed cache space shared by multiple tenants
2. each tenant has an upper-bound of cache size it can use
3. within each tenant's limit, an LRU replacement policy is used

Each tenant is identified by a unique "tag," which could be an integer or a pointer to some struct.

```C++
#include <gcache/hash.h>
#include <gcache/shared_cache.h>

struct Tenant {
  std::string name;
  // skip other fields...
};

char* page_cache = new char[4096 * 20];  // allocate a 20-page cache

using Cache_t = gcache::SharedCache</*Tag_t*/ Tenant*, /*Key_t*/ uint32_t,
                                    /*Value_t*/ char*,
                                    /*Hash*/ gcache::ghash>;
Cache_t lru_cache;

Tenant* t1 = new Tenant{"tenant-1"};
Tenant* t2 = new Tenant{"tenant-2"};

// init: 1) set cache capacity: tenant-1 has 8-page cache; tenant-2 has 12-page
// cache; 2) put page cache pointer into buffer
lru_cache.init(/*vector of <tag, capacity>*/ {{t1, 8}, {t2, 12}},
                /* init_func*/
                [&, i = 0l](Cache_t::Handle_t handle) mutable {
                  *handle = page_cache + (i++) * 4096;
                });

// add block 1 & 2 to tenant-1
auto h1 = lru_cache.insert(/*tag*/ t1, /*key: block_num*/ 1);
memcpy(/*buf*/ *h1, "This is block 1", 16);
auto h2 = lru_cache.insert(t1, 2);
memcpy(*h2, "This is block 2", 16);

// add block 3 to tenant-2
auto h3 = lru_cache.insert(t2, 3);
memcpy(*h3, "This is block 3", 16);

// since the cache pool is shared, `lookup` does not requie tag
h2 = lru_cache.lookup(2);
assert(memcmp(*h2, "This is block 2", 16) == 0);

// `relocate` could move some cache capacity from one tenant to another tenant
// move 2 block capacity from tenant-2 to tenant-1
size_t num_relocated = lru_cache.relocate(/*src*/ t2, /*dst*/ t1, /*size*/ 2);
assert(num_relocated == 2);

size_t t1_size = lru_cache.capacity_of(t1);
size_t t2_size = lru_cache.capacity_of(t2);
assert(t1_size == /*init_size*/ 8 + /*relocated*/ 2);
assert(t2_size == /*init_size*/ 12 - /*relocated*/ 2);
```

## Credits

gcache uses a modified version of the LRU page cache from Google's [LevelDB](https://github.com/google/leveldb).
