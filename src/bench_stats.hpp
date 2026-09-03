#pragma once
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <vector>

namespace pokex::bench {

// Latency samples, kept raw so percentiles are exact rather than interpolated
// out of buckets. At four bytes a sample, a million messages costs 4MB, which is
// cheaper than being approximately right.
//
// A log-bucketed histogram is derived from the same samples for plotting, since
// a linear histogram of a heavy-tailed distribution is unreadable.
class Samples {
 public:
  void reserve(std::size_t n) { values_.reserve(n); }
  void add(std::uint32_t ns) { values_.push_back(ns); }
  std::size_t count() const { return values_.size(); }

  void finalize() { std::sort(values_.begin(), values_.end()); }

  // Requires finalize() first.
  std::uint32_t percentile(double p) const {
    if (values_.empty()) return 0;
    const double rank = (p / 100.0) * static_cast<double>(values_.size() - 1);
    const auto idx = static_cast<std::size_t>(rank + 0.5);
    return values_[std::min(idx, values_.size() - 1)];
  }
  std::uint32_t min() const { return values_.empty() ? 0 : values_.front(); }
  std::uint32_t max() const { return values_.empty() ? 0 : values_.back(); }

  double mean() const {
    if (values_.empty()) return 0;
    long double total = 0;
    for (std::uint32_t v : values_) total += v;
    return static_cast<double>(total / static_cast<long double>(values_.size()));
  }

  // Log-bucketed histogram: one bucket per power of two, subdivided into eight,
  // giving roughly 12.5% resolution across the whole range.
  struct Bucket {
    std::uint32_t low{};
    std::uint32_t high{};
    std::uint64_t count{};
  };

  std::vector<Bucket> histogram() const {
    constexpr int kSub = 8;
    std::vector<Bucket> out;
    if (values_.empty()) return out;
    std::vector<std::uint64_t> counts(64 * kSub, 0);
    for (std::uint32_t v : values_) counts[bucket_of(v)]++;
    for (std::size_t b = 0; b < counts.size(); ++b) {
      if (!counts[b]) continue;
      out.push_back(Bucket{bucket_low(b), bucket_low(b + 1), counts[b]});
    }
    return out;
  }

 private:
  static std::size_t bucket_of(std::uint32_t v) {
    if (v < 8) return v;
    const int e = 31 - std::countl_zero(v);      // floor(log2(v))
    const std::uint32_t sub = (v >> (e - 3)) & 7u;  // three bits below the leader
    return static_cast<std::size_t>(e) * 8 + sub;
  }
  static std::uint32_t bucket_low(std::size_t b) {
    if (b < 8) return static_cast<std::uint32_t>(b);
    const std::size_t e = b / 8;
    const std::size_t sub = b % 8;
    return static_cast<std::uint32_t>((8 + sub) << (e - 3));
  }

  std::vector<std::uint32_t> values_{};
};

// The clock is neither free nor infinitely precise, and on Apple Silicon the
// precision is the binding constraint: steady_clock is backed by a 24MHz timer,
// so it advances in steps of about 41.7ns. An operation that takes ~50ns cannot
// be timed individually against a clock that coarse.
//
// Reporting both numbers is the difference between a benchmark and a boast:
// `overhead_ns` is what calling the clock costs, `granularity_ns` is the
// smallest difference it can express. Percentiles below a few multiples of the
// granularity are measuring the clock, not the code.
struct ClockFacts {
  std::uint32_t overhead_ns{};
  std::uint32_t granularity_ns{};
};

inline ClockFacts measure_clock(std::size_t iterations = 400000) {
  using Clock = std::chrono::steady_clock;
  std::uint64_t total = 0;
  std::uint32_t smallest_step = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < iterations; ++i) {
    const auto a = Clock::now();
    const auto b = Clock::now();
    const auto d = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
    total += d;
    if (d > 0 && d < smallest_step) smallest_step = d;
  }
  ClockFacts f;
  f.overhead_ns = static_cast<std::uint32_t>(total / iterations);
  f.granularity_ns = (smallest_step == 0xFFFFFFFFu) ? 0 : smallest_step;
  return f;
}

}  // namespace pokex::bench
