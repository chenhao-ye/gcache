#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "gcache/ghost_cache.h"
#include "gcache/handle.h"

using namespace gcache;

void test1() {
  GhostCache ghost_cache(1, 3, 6);

  ghost_cache.access(0);
  ghost_cache.access(1);
  ghost_cache.access(2);
  ghost_cache.access(3);
  std::cout << "Expect: Boundaries: [1, 0, (null)]; Stat: [0/4, 0/4, 0/4]\n";
  std::cout << ghost_cache;

  ghost_cache.access(4);
  ghost_cache.access(5);
  std::cout << "Expect: Boundaries: [3, 2, 1]; Stat: [0/6, 0/6, 0/6]\n";
  std::cout << ghost_cache;

  ghost_cache.access(2);
  ghost_cache.access(4);
  std::cout << "Expect: Boundaries: [5, 3, 1]; Stat: [1/8, 2/8, 2/8]\n";
  std::cout << ghost_cache;
}

void bench() {}

int main() {
  test1();
  bench();
  return 0;
}
