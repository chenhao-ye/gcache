#pragma once

#include <cstdint>

#if defined(__SSE4_2__)
#include <nmmintrin.h>  // for _mm_crc32_u32 instruction
#define crc32_u8 _mm_crc32_u8
#define crc32_u16 _mm_crc32_u16
#define crc32_u32 _mm_crc32_u32
#define crc32_u64 _mm_crc32_u64
#elif defined(__ARM_FEATURE_CRC32)
#define crc32_u8 __builtin_aarch64_crc32cb
#define crc32_u16 __builtin_aarch64_crc32ch
#define crc32_u32 __builtin_aarch64_crc32cw
#define crc32_u64 __builtin_aarch64_crc32cx
#else
#error "Unsupported architecture"
#endif

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

/* Hash for uint32_t */

struct ghash {  // default hash function for gcache
  uint32_t operator()(uint32_t x) const noexcept { return crc32_u32(0x537, x); }
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

/* Hash for string */

// https://stackoverflow.com/questions/53871074/how-to-implement-a-hash-function-for-strings-in-c-using-crc32c-instruction-from

static inline uint64_t byte_crc32(unsigned int crc, const char **buf,
                                  size_t len) {
  while (len > 0) {
    crc = crc32_u8(crc, *(const unsigned char *)(*buf));
    ++*buf;
    --len;
  }
  return crc;
}

static inline uint64_t hw_crc32(unsigned int crc, const char **buf,
                                size_t len) {
  while (len > 0) {
    crc = crc32_u16(crc, *(const uint16_t *)(*buf));
    *buf += 2;
    --len;
  }
  return crc;
}

static inline uint64_t word_crc32(unsigned int crc, const char **buf,
                                  size_t len) {
  while (len > 0) {
    crc = crc32_u32(crc, *(const uint32_t *)(*buf));
    *buf += 4;
    --len;
  }
  return crc;
}

static inline uint64_t dword_crc32(uint64_t crc, const char **buf, size_t len) {
  while (len > 0) {
    crc = crc32_u64(crc, *(const uint64_t *)(*buf));
    *buf += 8;
    --len;
  }
  return crc;
}

static inline uint64_t str_crc32(const char *buf, size_t len) {
  const size_t dword_chunks = len / 8;
  const size_t dword_diff = len % 8;

  const size_t word_chunks = dword_diff / 4;
  const size_t word_diff = dword_diff % 4;

  const size_t hw_chunks = word_diff / 2;
  const size_t hw_diff = word_diff % 2;

  uint64_t crc = byte_crc32(0, &buf, hw_diff);
  crc = hw_crc32(crc, &buf, hw_chunks);
  crc = word_crc32(crc, &buf, word_chunks);
  crc = dword_crc32(crc, &buf, dword_chunks);

  return crc;
}

struct ghash_str {
  uint32_t operator()(const char *buf, size_t len) const noexcept {
    return str_crc32(buf, len);
  };
};

}  // namespace gcache
