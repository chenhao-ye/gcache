#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "gcache/lru_cache.h"
#include "util.h"

using namespace gcache;

struct hash1 {
  uint32_t operator()(uint32_t x) const noexcept { return x + 1000; }
};

struct hash2 {
  uint32_t operator()(uint32_t x) const noexcept { return x; }
};

void test() {
  LRUCache<uint32_t, uint32_t, hash1> cache;
  cache.init(4);
  assert(cache.size() == 0);

  auto h1 = cache.insert(1, true);
  assert(h1);
  assert(cache.size() == 1);
  *h1 = 111;
  auto h2 = cache.insert(2, true);
  assert(h2);
  assert(cache.size() == 2);
  *h1 = 222;
  auto h3 = cache.insert(3, true);
  assert(h3);
  assert(cache.size() == 3);
  *h1 = 333;
  auto h4 = cache.insert(4);
  assert(h4);
  assert(cache.size() == 4);
  *h1 = 444;
  std::cout << "=== Expect: lru: [4], in_use: [1, 2, 3] ===\n";
  std::cout << cache;

  h4 = cache.lookup(4, true);
  *h4 = 4444;
  assert(cache.size() == 4);
  std::cout << "=== Expect: lru: [], in_use: [1, 2, 3, 4] ===\n";
  std::cout << cache;

  auto h5 = cache.insert(5, true);
  if (h5 != nullptr)
    throw std::runtime_error("Overflow insertion is not denied!");
  assert(cache.size() == 4);

  cache.release(h3);
  h5 = cache.insert(5, true);
  assert(h5);
  assert(cache.size() == 4);
  *h5 = 555;
  std::cout << "\n=== Expect: in_use: [1, 2, 4, 5] ===\n";
  std::cout << cache;

  cache.release(h5);
  cache.release(h2);
  cache.release(h4);
  assert(cache.size() == 4);
  std::cout << "\n=== Expect: lru: [5, 2, 4], in_use: [1] ===\n";
  std::cout << cache;

  h3 = cache.insert(3, true);
  assert(h3);
  assert(cache.size() == 4);
  *h3 = 3333;
  h5 = cache.lookup(5, true);
  assert(cache.size() == 4);
  std::cout << "\n=== Expect: lru: [2, 4], in_use: [1, 3] ===\n";
  std::cout << cache;
  if (h5 != nullptr)
    throw std::runtime_error("Expected evicted handle remains in cache!");

  h5 = cache.insert(5, true);
  assert(cache.size() == 4);
  std::cout << "\n=== Expect: lru: [4], in_use: [1, 3, 5] ===\n";
  std::cout << cache;

  auto h6 = cache.insert(6, true);
  assert(h6);
  assert(cache.size() == 4);
  *h6 = 666;
  std::cout << "\n=== Expect: lru: [], in_use: [1, 3, 5, 6] ===\n";
  std::cout << cache;

  auto h5_ = cache.insert(5, true);
  assert(h5_ == h5);
  assert(cache.size() == 4);
  *h5_ = 555;
  std::cout << "\n=== Expect: lru: [], in_use: [1, 3, 5, 6] ===\n";
  std::cout << cache;

  auto h7 = cache.insert(7, true);
  assert(cache.size() == 4);
  std::cout << "\n=== Expect: lru: [], in_use: [1, 3, 5, 6] ===\n";
  std::cout << cache;
  if (h7) throw std::runtime_error("Overflow handle is not denied!");

  cache.release(h1);
  cache.release(h3);
  cache.release(h5);
  cache.release(h6);
  assert(cache.size() == 4);
  std::cout << "\n=== Expect: lru: [1, 3, 6], in_use: [5] ===\n";
  std::cout << cache;

  cache.release(h5_);
  assert(cache.size() == 4);
  std::cout << "\n=== Expect: lru: [1, 3, 6, 5], in_use: [] ===\n";
  std::cout << cache;

  h7 = cache.lookup(7);
  if (h7) throw std::runtime_error("Lookup nonexisting handle is not denied!");

  h7 = cache.insert(7);
  assert(h7);
  assert(cache.size() == 4);
  *h7 = 777;
  std::cout << "\n=== Expect: lru: [3, 6, 5, 7], in_use: [] ===\n";
  std::cout << cache;

  // test erase/install
  bool success = cache.erase(h7);
  assert(success);
  assert(cache.size() == 3);
  std::cout << "\n=== Expect: lru: [3, 6, 5], in_use: [] ===\n";
  std::cout << cache;

  h6 = cache.lookup(6, true);
  assert(h6);
  assert(cache.size() == 3);
  std::cout << "\n=== Expect: lru: [3, 5], in_use: [6] ===\n";
  std::cout << cache;
  success = cache.erase(h6);
  if (success) throw std::runtime_error("Erase in-use handle is not denied!");

  auto h8 = cache.insert(8);
  *h8 = 888;
  assert(cache.size() == 3);
  std::cout << "\n=== Expect: lru: [5, 8], in_use: [6] ===\n";
  std::cout << cache;

  auto h9 = cache.install(9);
  assert(h9);
  assert(cache.size() == 4);
  *h9 = 999;
  std::cout << "\n=== Expect: lru: [5, 8, 9], in_use: [6] ===\n";
  std::cout << cache;

  cache.release(h6);

  std::cout << std::flush;
}

void bench() {
  LRUCache<uint32_t, uint32_t, hash2> cache;
  cache.init(256 * 1024);  // #blocks for 1GB working set

  // filling the cache
  auto ts0 = rdtsc();
  for (int i = 0; i < 256 * 1024; ++i) cache.insert(i);
  auto ts1 = rdtsc();

  // cache hit
  for (int i = 0; i < 256 * 1024; ++i) cache.insert(i);
  auto ts2 = rdtsc();

  // cache miss
  for (int i = 0; i < 256 * 1024; ++i) cache.insert(i + 256 * 1024);
  auto ts3 = rdtsc();

  std::cout << "Fill: " << (ts1 - ts0) / (256 * 1024) << " cycles/op\n";
  std::cout << "Hit:  " << (ts2 - ts1) / (256 * 1024) << " cycles/op\n";
  std::cout << "Miss: " << (ts3 - ts2) / (256 * 1024) << " cycles/op\n";
  std::cout << std::flush;
}

int main() {
  test();   // for correctness
  bench();  // for performance
  return 0;
}
