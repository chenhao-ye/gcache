#include <cassert>
#include <vector>

#include "gcache/shared_cache.h"
#include "util.h"

using namespace gcache;

void test1() {
  SharedCache<int, int, int> shared_cache;
  std::vector<std::pair<int, size_t>> tenant_configs;
  tenant_configs.emplace_back(537, 3);
  tenant_configs.emplace_back(564, 2);

  shared_cache.init(tenant_configs);

  auto h = shared_cache.insert(537, 1, 1001, true);
  assert(h);
  *h = 111;
  shared_cache.release(h);
  h = shared_cache.insert(564, 2, 1002, false);
  assert(h);
  *h = 222;
  h = shared_cache.insert(537, 3, 1003, false);
  assert(h);
  *h = 333;

  std::cout << "Expect: { 564: [2], 537: [1, 3]}" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 4, 1004, false);
  assert(h);
  *h = 444;
  h = shared_cache.insert(537, 5, 1005, false);
  assert(h);
  *h = 555;
  std::cout << "Expect: { 564: [2, 4], 537: [1, 3, 5] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 6, 1006, false);
  assert(h);
  *h = 666;
  h = shared_cache.insert(537, 2, 1002, false);
  assert(h);
  *h = 2222;
  std::cout << "Expect: { 564: [4, 6], 537: [3, 5, 2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  // try to access a handle already in the other tenant's cache
  // expect to just return the existing one
  h = shared_cache.insert(564, 2, 1002, false);
  assert(h);
  *h = 22222;
  std::cout << "Expect: { 564: [4, 6], 537: [3, 5, 2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  shared_cache.relocate(537, 564, 2);
  std::cout << "Expect: { 564: [4, 6], 537: [2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 7, 1007, false);
  assert(h);
  *h = 777;
  h = shared_cache.insert(564, 8, 1008, false);
  assert(h);
  *h = 888;
  std::cout << "Expect: { 564: [4, 6, 7, 8], 537: [2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 9, 1009, false);
  assert(h);
  *h = 999;
  std::cout << "Expect: { 564: [6, 7, 8, 9], 537: [2] }" << std::endl;
  std::cout << shared_cache << std::endl;
}

int main() {
  test1();
  return 0;
}
