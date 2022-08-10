#include <sys/time.h>

#include <cstddef>
#include <cstdint>

uint64_t now_micros() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

uint64_t rdtsc() {
  size_t lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return (((uint64_t)hi << 32) | lo);
}
