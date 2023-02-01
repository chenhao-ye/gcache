#pragma once

#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

static off_t div_ceil(off_t a, off_t b) { return (a + b - 1) / b; }
static off_t round_up(off_t a, off_t b) { return div_ceil(a, b) * b; }
static off_t round_down(off_t a, off_t b) { return a - a % b; }

enum class OffsetType { UNIF, ZIPF, SEQ };

struct BaseGenerator {
  size_t index;
  BaseGenerator() : index(0) {}
  BaseGenerator(const BaseGenerator&) = delete;
  virtual ~BaseGenerator() = default;
  virtual off_t get() = 0;
  void next() { index++; }
};

struct SeqGenerator : public BaseGenerator {
  const off_t min;
  const size_t repeat;
  const off_t align;
  SeqGenerator(off_t min, off_t max, off_t align)
      : min(round_up(min, align)),
        repeat((round_down(max - 1, align) - round_up(min, align)) / align + 1),
        align(align) {}
  off_t get() { return min + index % repeat * align; }
};

struct UnifGenerator : public BaseGenerator {
  std::mt19937 rng{/*seed*/ 0x537};
  std::uniform_int_distribution<off_t> dist;
  const off_t align;
  UnifGenerator(off_t min, off_t max, off_t align)
      : dist(div_ceil(min, align), (max - 1) / align), align(align) {}
  off_t get() { return dist(rng) * align; }
};

struct ZipfGenerator : public BaseGenerator {
  std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<double> dist{0, 1};
  const off_t align;
  const double theta;
  const uint64_t n;
  const off_t min;
  const double denom;
  const double eta;
  ZipfGenerator(off_t min, off_t max, double theta, off_t align)
      : align(align),
        theta(theta),
        n((max - min) / align),
        min(round_up(min, align)),
        denom(zeta(n, theta)),
        eta((1 - std::pow(2.0 / n, 1 - theta)) / (1 - zeta(2, theta) / denom)) {
  }
  off_t get() {
    double u = dist(rng);
    double uz = u * denom;
    if (uz < 1.0) {
      return min;
    } else if (uz < 1.0 + std::pow(0.5, theta)) {
      return min + align;
    } else {
      double alpha = 1.0 / (1.0 - theta);
      return min + align * uint64_t(n * std::pow(eta * u - eta + 1, alpha));
    }
  }

  static double zeta(uint64_t n, double theta) {
    double sum = 0;
    for (uint64_t i = 1; i <= n; i++) sum += std::pow(1.0 / i, theta);
    return sum;
  }
};

struct Offsets {
  size_t num;
  BaseGenerator* gen;

  struct EndIterator : std::iterator<std::input_iterator_tag, off_t> {
    size_t num;
    explicit EndIterator(size_t num) : num(num) {}
  };

  struct Iterator : std::iterator<std::input_iterator_tag, off_t> {
    BaseGenerator& gen;
    explicit Iterator(BaseGenerator& gen) : gen(gen) {}
    Iterator& operator++() {
      gen.next();
      return *this;
    }
    off_t operator*() const { return gen.get(); }
    bool operator!=(const EndIterator& other) const {
      return gen.index < other.num;
    }
  };

  Offsets(size_t num, const OffsetType type, uint64_t size, off_t align,
          double zipf_theta = 0)
      : num(num), gen(get_generator(type, size, align, zipf_theta)) {}
  Offsets(const Offsets&) = delete;
  ~Offsets() { delete gen; }

  [[nodiscard]] Iterator begin() const { return Iterator(*gen); }
  [[nodiscard]] EndIterator end() const { return EndIterator(num); }
  [[nodiscard]] size_t size() const { return num; }

  static BaseGenerator* get_generator(const OffsetType type, uint64_t size,
                                      off_t align, double zipf_theta = 0) {
    switch (type) {
      case OffsetType::SEQ:
        return new SeqGenerator(0, size - 1, align);
      case OffsetType::UNIF:
        return new UnifGenerator(0, size - 1, align);
      case OffsetType::ZIPF:
        return new ZipfGenerator(0, size - 1, zipf_theta, align);
      default:
        throw std::runtime_error("Unimplemented offset type");
    }
  }
};
