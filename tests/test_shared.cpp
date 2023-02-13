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

  std::cout << "Expect: { 564: [2], 537: [1, 3]}" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 4, false);
  assert(h);
  *h = 444;
  h = shared_cache.insert(537, 5, false);
  assert(h);
  *h = 555;
  std::cout << "Expect: { 564: [2, 4], 537: [1, 3, 5] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 6, false);
  assert(h);
  *h = 666;
  h = shared_cache.insert(537, 2, false);
  assert(h);
  *h = 2222;
  std::cout << "Expect: { 564: [4, 6], 537: [3, 5, 2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  // try to access a handle already in the other tenant's cache
  // expect to just return the existing one
  h = shared_cache.insert(564, 2, false);
  assert(h);
  *h = 22222;
  std::cout << "Expect: { 564: [4, 6], 537: [3, 5, 2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  shared_cache.relocate(537, 564, 2);
  std::cout << "Expect: { 564: [4, 6], 537: [2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 7, false);
  assert(h);
  *h = 777;
  h = shared_cache.insert(564, 8, false);
  assert(h);
  *h = 888;
  std::cout << "Expect: { 564: [4, 6, 7, 8], 537: [2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  h = shared_cache.insert(564, 9, false);
  assert(h);
  *h = 999;
  std::cout << "Expect: { 564: [6, 7, 8, 9], 537: [2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  // test export/import
  bool success = shared_cache.export_node(h);
  assert(success);
  std::cout << "Expect: { 564: [6, 7, 8], 537: [2] }" << std::endl;
  std::cout << shared_cache << std::endl;

  shared_cache.import_node(537, 10);
  shared_cache.import_node(537, 11);
  shared_cache.import_node(564, 12);
  std::cout << "Expect: { 564: [6, 7, 8, 12], 537: [2, 10, 11] }" << std::endl;
  std::cout << shared_cache << std::endl;
}

int main() {
  test1();
  return 0;
}
