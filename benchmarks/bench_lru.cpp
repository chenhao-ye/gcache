#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "gcache/lru_cache.h"

using namespace gcache;

void test() {
  LRUCache<uint32_t, uint64_t> cache;
  cache.init_pool(4);

  auto h1 = cache.insert(1, 1001);
  assert(h1);
  h1->value = 111;
  auto h2 = cache.insert(2, 1002);
  assert(h2);
  h2->value = 222;
  auto h3 = cache.insert(3, 1003);
  assert(h3);
  h3->value = 333;
  auto h4 = cache.insert(4, 1004);
  assert(h4);
  h4->value = 444;
  std::cout << "=== Expect: in_use: [1, 2, 3, 4] ===\n";
  std::cout << cache;

  auto h5 = cache.insert(5, 1005);
  if (h5 != nullptr)
    throw std::runtime_error("Overflow insertion is not denied!");

  cache.release(h3);
  h5 = cache.insert(5, 1005);
  assert(h5);
  h5->value = 555;
  std::cout << "\n=== Expect: in_use: [1, 2, 4, 5] ===\n";
  std::cout << cache;

  cache.release(h5);
  cache.release(h2);
  cache.release(h4);
  std::cout << "\n=== Expect: lru: [5, 2, 4], in_use: [1] ===\n";
  std::cout << cache;

  h3 = cache.insert(3, 1003);
  assert(h3);
  h3->value = 3333;
  h5 = cache.lookup(5, 1005);
  std::cout << "\n=== Expect: lru: [2, 4], in_use: [1, 3] ===\n";
  std::cout << cache;
  if (h5 != nullptr)
    throw std::runtime_error("Expected evicted handle remains in cache!");

  cache.erase(3, 1003);
  std::cout << "\n=== Expect: lru: [2, 4], in_use: [1] ===\n";
  std::cout << cache;

  h5 = cache.insert(5, 1005);
  std::cout << "\n=== Expect: lru: [2, 4], in_use: [1] ===\n";
  std::cout << cache;
  if (h5 != nullptr) throw std::runtime_error("Unreleased handle is freed!");
  cache.release(h3);

  h5 = cache.insert(5, 1005);
  assert(h5);
  h5->value = 5555;
  std::cout << "\n=== Expect: lru: [2, 4], in_use: [1, 5] ===\n";
  std::cout << cache;

  auto h5_ = cache.insert(5, 1005);
  assert(h5_);
  h5->value = 55555;
  std::cout << "\n=== Expect: lru: [4], in_use: [1, 5] ===\n";
  std::cout << cache;
}

void bench() {
  // auto cache = new LRUCache<uint32_t, uint64_t>();
  // cache->init_pool(1024 * 256);  // #blocks for 1GB working set
}

int main() {
  test();   // for correctness
  bench();  // for performance
  return 0;
}
