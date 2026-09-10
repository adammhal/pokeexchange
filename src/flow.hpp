#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string_view>
#include <vector>

#include "pokex/events.hpp"
#include "pokex/types.hpp"

// Order flow generation, shared by the benchmark and the cache profiler so both
// measure the same thing.
namespace pokex::bench {

// A single number would misrepresent these data structures, because which one
// wins depends entirely on the shape of the book.
//
//   - A WIDE book spends its time finding the right price level, so the array
//     ladder pays off and the tree does not.
//   - A DEEP book spends its time walking a level looking for an order id, so
//     only the hash index pays off, and the intrusive list is actively harmful
//     because a shared pool has worse traversal locality than a deque's
//     contiguous blocks.
//
// Both shapes occur in real markets: wide and thin looks like an illiquid name,
// narrow and deep looks like a liquid future with a one-tick spread.
struct Scenario {
  const char* name;
  const char* description;
  std::size_t resident;
  std::size_t levels;
  int cancel_pct;
};

inline constexpr Scenario kScenarios[] = {
    {"wide", "many price levels, shallow depth (illiquid name)", 500'000, 60'000, 40},
    {"mixed", "a few thousand levels, moderate depth", 500'000, 4'096, 90},
    {"deep", "few levels, enormous depth (liquid future)", 500'000, 64, 90},
};

struct FlowOptions {
  std::size_t messages = 1'000'000;  // messages generated per scenario
  std::uint64_t seed = 0xBEEF;
};

struct Mix {
  std::size_t new_orders = 0;
  std::size_t cancels = 0;
  std::size_t market_orders = 0;
  std::size_t aggressive = 0;
};

// preload builds a large resting book; steady is what actually gets timed.
//
// The shape here matters more than it looks. An earlier version of this
// benchmark used 201 price levels and a few thousand resting orders, which meant
// v0's std::map fit entirely in L1 cache and every price level held about a
// dozen orders. Under those conditions none of v1, v2 or v3 can possibly show an
// advantage, because neither cache misses nor long level scans ever happen. A
// benchmark that cannot distinguish the thing it exists to measure is worthless.
struct Flow {
  std::vector<Command> preload;
  std::vector<Command> steady;
};

inline Flow build_flow(const FlowOptions& opt, const Scenario& sc, Mix& mix) {
  std::mt19937_64 rng(opt.seed);
  auto uni = [&](int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
  };

  const Price mid = 32768;
  const int half = std::max(1, static_cast<int>(sc.levels / 2));
  std::vector<OrderId> live;
  live.reserve(sc.resident * 2);
  OrderId next = 1;

  // A passive order rests away from the mid on its own side, so it does not
  // trade on arrival and the book actually accumulates depth.
  auto passive = [&](OrderId id) {
    const Side side = uni(0, 1) ? Side::Buy : Side::Sell;
    const int offset = 1 + uni(0, half - 1);
    const Price price = (side == Side::Buy) ? mid - offset : mid + offset;
    return NewOrder{id, side, OrderType::Limit, price, static_cast<Quantity>(uni(1, 200))};
  };

  Flow flow;
  flow.preload.reserve(sc.resident);
  for (std::size_t i = 0; i < sc.resident; ++i) {
    const OrderId id = next++;
    live.push_back(id);
    flow.preload.push_back(passive(id));
  }

  flow.steady.reserve(opt.messages);
  for (std::size_t i = 0; i < opt.messages; ++i) {
    const bool can_cancel = !live.empty();
    if (can_cancel && uni(0, 99) < sc.cancel_pct) {
      // Cancel a uniformly random resting order, which is what makes the id
      // lookup the hot path rather than an afterthought.
      const auto slot = static_cast<std::size_t>(uni(0, static_cast<int>(live.size()) - 1));
      flow.steady.push_back(CancelOrder{live[slot]});
      live[slot] = live.back();
      live.pop_back();
      ++mix.cancels;
      continue;
    }
    const OrderId id = next++;
    const int roll = uni(0, 99);
    if (roll < 4) {  // market order: sweeps whatever is at the top
      flow.steady.push_back(NewOrder{id, uni(0, 1) ? Side::Buy : Side::Sell,
                                     OrderType::Market, 0,
                                     static_cast<Quantity>(uni(1, 400))});
      ++mix.market_orders;
      ++mix.new_orders;
      continue;
    }
    if (roll < 10) {  // aggressive limit: crosses a few levels, then rests
      const Side side = uni(0, 1) ? Side::Buy : Side::Sell;
      const int reach = uni(1, 8);
      const Price price = (side == Side::Buy) ? mid + reach : mid - reach;
      flow.steady.push_back(NewOrder{id, side, OrderType::Limit, price,
                                     static_cast<Quantity>(uni(1, 300))});
      live.push_back(id);
      ++mix.aggressive;
      ++mix.new_orders;
      continue;
    }
    live.push_back(id);
    flow.steady.push_back(passive(id));
    ++mix.new_orders;
  }
  return flow;
}


inline const Scenario* find_scenario(const char* name) {
  for (const Scenario& sc : kScenarios)
    if (std::string_view(sc.name) == name) return &sc;
  return nullptr;
}

}  // namespace pokex::bench
