#pragma once

#if defined(__SSE4_2__)
#include <nmmintrin.h>
#define crc_u32(x) _mm_crc32_u32(/*seed*/ 0x537, x)
#elif defined(__ARM_FEATURE_CRC32)
#define crc_u32(x) __builtin_aarch64_crc32cw(/*seed*/ 0x537, x)
#else
#error "Unsupported architecture"
#endif

#include <cstdint>

namespace gcache {

/**
 * Here we tested on several implementation of hash.
 * The exposed one is defined as a struct `gcache::ghash`.
 */

// From XXHash:
// https://github.com/Cyan4973/xxHash/blob/release/xxhash.h#L1968
[[maybe_unused]] static inline uint32_t xxhash_u32(uint32_t x) {
  x ^= x >> 15;
  x *= 0x85EBCA77U;
  x ^= x >> 13;
  x *= 0xC2B2AE3DU;
  x ^= x >> 16;
  return x;
}

// From MurmurHash:
// https://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp#L68
[[maybe_unused]] static inline uint32_t murmurhash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x85ebca6b;
  x ^= x >> 13;
  x *= 0xc2b2ae35;
  x ^= x >> 16;
  return x;
}

struct ghash {  // default hash function for gcache
  uint32_t operator()(uint32_t x) const noexcept { return crc_u32(x); }
};

struct idhash {  // identical mapping
  uint32_t operator()(uint32_t x) const noexcept { return x; }
};

struct xxhash {
  uint32_t operator()(uint32_t x) const noexcept { return xxhash_u32(x); }
};

struct murmurhash {
  uint32_t operator()(uint32_t x) const noexcept { return murmurhash_u32(x); }
};

}  // namespace gcache
