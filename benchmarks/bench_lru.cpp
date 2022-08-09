#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "gcache/lru_cache.h"

using namespace gcache;

void test() {
  LRUCache<uint32_t, uint64_t> cache;
  cache.init(4);

  auto h1 = cache.insert(1, 1001, true);
  assert(h1);
  h1->value = 111;
  auto h2 = cache.insert(2, 1002, true);
  assert(h2);
  h2->value = 222;
  auto h3 = cache.insert(3, 1003, true);
  assert(h3);
  h3->value = 333;
  auto h4 = cache.insert(4, 1004);
  assert(h4);
  h4->value = 444;
  std::cout << "=== Expect: lru: [4], in_use: [1, 2, 3] ===\n";
  std::cout << cache;

  h4 = cache.lookup(4, 1004, true);
  h4->value = 4444;
  std::cout << "=== Expect: lru: [], in_use: [1, 2, 3, 4] ===\n";
  std::cout << cache;

  auto h5 = cache.insert(5, 1005, true);
  if (h5 != nullptr)
    throw std::runtime_error("Overflow insertion is not denied!");

  cache.release(h3);
  h5 = cache.insert(5, 1005, true);
  assert(h5);
  h5->value = 555;
  std::cout << "\n=== Expect: in_use: [1, 2, 4, 5] ===\n";
  std::cout << cache;

  cache.release(h5);
  cache.release(h2);
  cache.release(h4);
  std::cout << "\n=== Expect: lru: [5, 2, 4], in_use: [1] ===\n";
  std::cout << cache;

  h3 = cache.insert(3, 1003, true);
  assert(h3);
  h3->value = 3333;
  h5 = cache.lookup(5, 1005, true);
  std::cout << "\n=== Expect: lru: [2, 4], in_use: [1, 3] ===\n";
  std::cout << cache;
  if (h5 != nullptr)
    throw std::runtime_error("Expected evicted handle remains in cache!");

  h5 = cache.insert(5, 1005, true);
  std::cout << "\n=== Expect: lru: [4], in_use: [1, 3, 5] ===\n";
  std::cout << cache;

  auto h6 = cache.insert(6, 1006, true);
  assert(h6);
  h6->value = 666;
  std::cout << "\n=== Expect: lru: [], in_use: [1, 3, 5, 6] ===\n";
  std::cout << cache;

  auto h5_ = cache.insert(5, 1005, true);
  assert(h5_ == h5);
  h5_->value = 5555;
  std::cout << "\n=== Expect: lru: [], in_use: [1, 3, 5, 6] ===\n";
  std::cout << cache;

  auto h7 = cache.insert(7, 1007, true);
  std::cout << "\n=== Expect: lru: [], in_use: [1, 3, 5, 6] ===\n";
  std::cout << cache;
  if (h7) throw std::runtime_error("Overflow handle is not denied!");

  cache.release(h1);
  cache.release(h3);
  cache.release(h5);
  cache.release(h6);
  std::cout << "\n=== Expect: lru: [1, 3, 6], in_use: [5] ===\n";
  std::cout << cache;

  cache.release(h5_);
  std::cout << "\n=== Expect: lru: [1, 3, 6, 5], in_use: [] ===\n";
  std::cout << cache;

  h7 = cache.lookup(7, 1007);
  if (h7) throw std::runtime_error("Lookup nonexisting handle is not denied!");

  h7 = cache.insert(7, 1007);
  assert(h7);
  h7->value = 777;
  std::cout << "\n=== Expect: lru: [3, 6, 5, 7], in_use: [] ===\n";
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
