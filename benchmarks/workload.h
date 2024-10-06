#pragma once

#include <cstdint>
#include <random>
#include <stdexcept>

enum class OffsetType { UNIF, ZIPF, SEQ };

struct BaseGenerator {
  size_t index;
  BaseGenerator() : index(0) {}
  BaseGenerator(const BaseGenerator&) = delete;
  virtual ~BaseGenerator() = default;
  virtual off_t get() = 0;
  void next() { index++; }
};

struct AlignGenerator : public BaseGenerator {
  const off_t min_;
  const off_t align_;
  /* We force min and max must be aligned.
   * This also ensure [output, output + align - 1] is also safe (i.e. < max)
   */
  AlignGenerator(off_t min, off_t max, off_t align) : min_(min), align_(align) {
    if (min % align != 0) throw std::runtime_error("min is not aligned!");
    if (max % align != 0) throw std::runtime_error("max is not aligned!");
  }
  off_t map(off_t x) { return min_ + align_ * x; }
};

struct SeqGenerator : public AlignGenerator {
  off_t n_;
  SeqGenerator(off_t min, off_t max, off_t align)
      : AlignGenerator(min, max, align), n_((max - min) / align) {}
  off_t get() override { return map(index % n_); }
};

struct UnifGenerator : public AlignGenerator {
  std::mt19937 rng;
  std::uniform_int_distribution<off_t> dist; /* inclusive */
  UnifGenerator(off_t min, off_t max, off_t align, uint64_t seed)
      : AlignGenerator(min, max, align),
        rng(seed),
        dist(0, (max - min) / align - 1) {}
  off_t get() override { return map(dist(rng)); }
};

struct ZipfGenerator : public AlignGenerator {
  std::mt19937 rng;
  std::uniform_real_distribution<double> dist{0.0, 1.0};
  const double theta_;
  const uint64_t n_;
  const double denom_;
  const double eta_;
  const double alpha_;
  // NOTE: member order matters: variable must be init in order!

  ZipfGenerator(off_t min, off_t max, double theta, off_t align, uint64_t seed)
      : AlignGenerator(min, max, align),
        rng(seed),
        theta_(theta),
        n_((max - min) / align),
        denom_(zeta(n_, theta)),
        eta_((1 - std::pow(2.0 / n_, 1 - theta)) /
             (1 - zeta(2, theta) / denom_)),
        alpha_(1.0 / (1.0 - theta)) {}
  off_t get() override {
    double u = dist(rng);
    double uz = u * denom_;
    if (uz < 1.0) return map(0);
    if (uz < 1.0 + std::pow(0.5, theta_)) return map(1);
    return map(n_ * std::pow(eta_ * u - eta_ + 1, alpha_));
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
          double zipf_theta, uint64_t seed)
      : num(num), gen(get_generator(type, size, align, zipf_theta, seed)) {}
  Offsets(const Offsets&) = delete;
  ~Offsets() { delete gen; }

  [[nodiscard]] Iterator begin() const { return Iterator(*gen); }
  [[nodiscard]] EndIterator end() const { return EndIterator(num); }
  [[nodiscard]] size_t size() const { return num; }

  static BaseGenerator* get_generator(const OffsetType type, uint64_t size,
                                      off_t align, double zipf_theta,
                                      uint64_t seed) {
    switch (type) {
      case OffsetType::SEQ:
        return new SeqGenerator(0, size, align);
      case OffsetType::UNIF:
        return new UnifGenerator(0, size, align, seed);
      case OffsetType::ZIPF:
        return new ZipfGenerator(0, size, zipf_theta, align, seed);
      default:
        throw std::runtime_error("Unimplemented offset type");
    }
  }
};
