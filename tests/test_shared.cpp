#include <cassert>
#include <vector>

#include "gcache/shared_cache.h"
#include "util.h"

using namespace gcache;

struct hash1 {
  uint32_t operator()(uint32_t x) const noexcept { return x + 1000; }
};

void test1() {
  SharedCache<int, int, int, hash1> shared_cache;
  std::vector<std::pair<int, size_t>> tenant_configs;
  tenant_configs.emplace_back(537, 3);
  tenant_configs.emplace_back(564, 2);

  shared_cache.init(tenant_configs);
  assert(shared_cache.capacity() == 5);
  assert(shared_cache.size_of(537) == 0);
  assert(shared_cache.size_of(564) == 0);

  [[maybe_unused]] auto& cache_537 = shared_cache.get_cache(537);
  [[maybe_unused]] auto& cache_564 = shared_cache.get_cache(564);
  assert(cache_537.size() == 0);
  assert(cache_564.size() == 0);

  auto h = shared_cache.insert(537, 1, true);
  assert(h);
  *h = 111;
  shared_cache.release(h);
  h = shared_cache.insert(564, 2, false);
  assert(h);
  *h = 222;
  h = shared_cache.insert(537, 3, false);
  assert(h);
  *h = 333;
  assert(cache_537.size() == 2);
  assert(cache_564.size() == 1);
  std::cout << "Expect: { 537: [1, 3], 564: [2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 4, false);
  assert(h);
  *h = 444;
  h = shared_cache.insert(537, 5, false);
  assert(h);
  *h = 555;
  assert(cache_537.size() == 3);
  assert(cache_564.size() == 2);
  std::cout << "Expect: { 537: [1, 3, 5], 564: [2, 4] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 6, false);
  assert(h);
  *h = 666;
  h = shared_cache.insert(537, 2, false);
  assert(h);
  *h = 2222;
  assert(cache_537.size() == 3);
  assert(cache_564.size() == 2);
  std::cout << "Expect: {  537: [3, 5, 2], 564: [4, 6] }" << std::endl;
  std::cout << shared_cache << std::endl;

  // try to access a handle already in the other tenant's cache
  // expect to just return the existing one
  h = shared_cache.insert(564, 2, false);
  assert(h);
  *h = 22222;
  assert(cache_537.size() == 3);
  assert(cache_564.size() == 2);
  std::cout << "Expect: { 537: [3, 5, 2], 564: [4, 6] }" << std::endl;
  std::cout << shared_cache << std::endl;

  shared_cache.relocate(537, 564, 2);
  assert(cache_537.size() == 1);
  assert(cache_564.size() == 2);
  std::cout << "Expect: { 537: [2], 564: [4, 6] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 7, false);
  assert(h);
  *h = 777;
  h = shared_cache.insert(564, 8, false);
  assert(h);
  *h = 888;
  assert(cache_537.size() == 1);
  assert(cache_564.size() == 4);
  std::cout << "Expect: { 537: [2], 564: [4, 6, 7, 8] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 9, false);
  assert(h);
  *h = 999;
  assert(cache_537.size() == 1);
  assert(cache_564.size() == 4);
  std::cout << "Expect: { 537: [2], 564: [6, 7, 8, 9]}" << std::endl;
  std::cout << shared_cache << std::endl;

  // test erase/install
  [[maybe_unused]] bool success = shared_cache.erase(h);
  assert(success);
  assert(cache_537.size() == 1);
  assert(cache_564.size() == 3);
  assert(shared_cache.size_of(537) == 1);
  assert(shared_cache.size_of(564) == 3);
  assert(shared_cache.capacity() == 4);
  std::cout << "Expect: { 537: [2], 564: [6, 7, 8] }" << std::endl;
  std::cout << shared_cache << std::endl;

  // setting values is skipped...
  shared_cache.install(537, 10);
  shared_cache.install(537, 11);
  shared_cache.install(564, 12);
  assert(cache_537.size() == 3);
  assert(cache_564.size() == 4);
  assert(shared_cache.size_of(537) == 3);
  assert(shared_cache.size_of(564) == 4);
  assert(shared_cache.capacity() == 7);
  std::cout << "Expect: { 537: [2, 10, 11], 564: [6, 7, 8, 12] }" << std::endl;
  std::cout << shared_cache << std::endl;
}

int main() {
  test1();
  return 0;
}
