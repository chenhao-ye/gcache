#pragma once

#include <cstdint>
#include <iomanip>
#include <limits>

namespace gcache {

struct CacheStat {
  uint64_t hit_cnt;
  uint64_t miss_cnt;

 public:
  CacheStat() : hit_cnt(0), miss_cnt(0) {}
  void add_hit() { ++hit_cnt; }
  void add_miss() { ++miss_cnt; }

  // we may read an inconsistent version if the reader thread is not the writer,
  // but most inaccuracy is tolerable, unless it produces a unreasonable value,
  // e.g., hit_rate > 100%.
  // we don't use atomic here because we find it is too expensive.
  [[nodiscard]] double get_hit_rate() const {
    uint64_t acc_cnt = hit_cnt + miss_cnt;
    if (acc_cnt == 0) return std::numeric_limits<double>::infinity();
    return double(hit_cnt) / double(acc_cnt);
  }

  [[nodiscard]] double get_miss_rate() const {
    uint64_t acc_cnt = hit_cnt + miss_cnt;
    if (acc_cnt == 0) return std::numeric_limits<double>::infinity();
    return double(miss_cnt) / double(acc_cnt);
  }

  void reset() {
    hit_cnt = 0;
    miss_cnt = 0;
  }

  std::ostream& print(std::ostream& os, int width = 0) const {
    uint64_t acc_cnt = hit_cnt + miss_cnt;
    if (acc_cnt == 0)
      return os << "  NAN (" << std::setw(width) << std::fixed << hit_cnt << '/'
                << std::setw(width) << std::fixed << acc_cnt << ')';
    os << std::setw(5) << std::fixed << std::setprecision(1)
       << get_hit_rate() * 100 << '%';
    return os << " (" << std::setw(width) << std::fixed << hit_cnt << '/'
              << std::setw(width) << std::fixed << acc_cnt << ')';
  }

  // print for debugging
  friend std::ostream& operator<<(std::ostream& os, const CacheStat& s) {
    return s.print(os, 0);
  }
};
}  // namespace gcache