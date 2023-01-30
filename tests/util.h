#include <sys/time.h>

#include <cstddef>
#include <cstdint>

uint64_t rdtsc() {
  size_t lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return (((uint64_t)hi << 32) | lo);
}
