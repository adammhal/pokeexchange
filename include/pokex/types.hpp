#pragma once
#include <cstddef>
#include <cstdint>

namespace pokex {

// A price is an integer count of ticks, never a float. Exact equality and exact
// ordering are required for price-time priority, and an array-indexed price
// ladder (v1) can only be indexed by an integer.
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

// The engine's only notion of time: a counter incremented once per accepted
// command. There is no clock anywhere in the engine, which is what makes the
// output byte-identical across runs and machines.
using Sequence = std::uint64_t;

// An array-indexed price ladder (v1 onward) can only be indexed by a bounded
// integer, so the tick domain is fixed here and enforced by the engine for every
// book version. Enforcing it only inside v1 would make v0 and v1 disagree on
// out-of-range input, which would break the cross-version equivalence the whole
// benchmark claim rests on.
inline constexpr Price kMinPrice = 1;
inline constexpr Price kMaxPrice = 65535;
inline constexpr std::size_t kPriceLevels = static_cast<std::size_t>(kMaxPrice) + 1;

enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t { Limit, Market };

constexpr Side opposite(Side s) noexcept {
  return s == Side::Buy ? Side::Sell : Side::Buy;
}

struct Order {
  OrderId id{};
  Side side{};
  OrderType type{};
  Price price{};        // meaningless when type == Market; never read in that case
  Quantity quantity{};  // as originally submitted
  Quantity remaining{}; // still outstanding
  Sequence seq{};       // arrival order, and therefore time priority
};

}  // namespace pokex
