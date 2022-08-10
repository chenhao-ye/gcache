#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "gcache/ghost_cache.h"
#include "gcache/handle.h"

using namespace gcache;

void test1() {
  GhostCache ghost_cache(1, 3, 6);
  std::cout << "=== Test 1 ===\n";

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
  std::cout << "Expect: Boundaries: [4, 3, 1]; Stat: [0/7, 1/7, 1/7]\n";
  std::cout << ghost_cache;

  ghost_cache.access(4);
  std::cout << "Expect: Boundaries: [5, 3, 1]; Stat: [1/8, 2/8, 2/8]\n";
  std::cout << ghost_cache;
}

void test2() {
  GhostCache ghost_cache(2, 2, 7);
  std::cout << "=== Test 2 ===\n";

  ghost_cache.access(0);
  ghost_cache.access(1);
  ghost_cache.access(2);
  ghost_cache.access(3);
  std::cout << "Expect: Boundaries: [2, 0, (null)]; Stat: [0/4, 0/4, 0/4]\n";
  std::cout << ghost_cache;

  ghost_cache.access(4);
  ghost_cache.access(5);
  std::cout << "Expect: Boundaries: [4, 2, 0]; Stat: [0/6, 0/6, 0/6]\n";
  std::cout << ghost_cache;

  ghost_cache.access(6);
  ghost_cache.access(7);
  std::cout << "Expect: Boundaries: [6, 4, 2]; Stat: [0/8, 0/8, 0/8]\n";
  std::cout << ghost_cache;

  ghost_cache.access(1);
  std::cout << "Expect: Boundaries: [7, 5, 3]; Stat: [0/9, 0/9, 0/9]\n";
  std::cout << ghost_cache;

  ghost_cache.access(4);
  std::cout << "Expect: Boundaries: [1, 6, 3]; Stat: [0/10, 0/10, 1/10]\n";
  std::cout << ghost_cache;
}

void bench() {}

int main() {
  test1();
  test2();
  bench();
  return 0;
}
