#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "pokex/book_concept.hpp"
#include "pokex/types.hpp"

namespace pokex {

// v1: the price ladder.
//
// One change from v0, deliberately isolated: the *level lookup* stops being a
// tree walk and becomes address arithmetic. A price is an integer count of
// ticks, so a price IS an array index. Neighbouring prices are neighbouring
// bytes, which the hardware prefetcher can see coming.
//
// The per-level queue is still a std::deque, exactly as in v0, so the only thing
// this version changes is tree-versus-array. v2 replaces the queue.
//
// Finding the best price could be a scan over 65,536 slots, which would be far
// worse than the tree it replaced. Instead each side keeps a bitmask of occupied
// levels, one bit per tick, and the best price is found with a hardware
// count-leading/trailing-zeros instruction over 64 levels at a time. The cached
// best_bid_/best_ask_ means the common case costs nothing at all: only when the
// best level empties out do we go looking.
//
// Cancel is UNCHANGED from v0: hash to (side, price), then linear-scan that
// level. That is on purpose. Cancels are roughly 90% of real traffic, so the
// benchmark should show v1 improving inserts and matching while barely moving
// cancel. That gap is what motivates v3.
class BookV1Ladder {
 public:
  BookV1Ladder() : bids_(kPriceLevels), asks_(kPriceLevels) {
    bid_occ_.fill(0);
    ask_occ_.fill(0);
  }

  std::optional<Price> best(Side side) const {
    const Price p = (side == Side::Buy) ? best_bid_ : best_ask_;
    if (p == kNoPrice) return std::nullopt;
    return p;
  }

  Order& front(Side side) {
    const Price p = (side == Side::Buy) ? best_bid_ : best_ask_;
    return levels(side)[static_cast<std::size_t>(p)].front();
  }

  void pop_front(Side side) {
    const Price p = (side == Side::Buy) ? best_bid_ : best_ask_;
    auto& queue = levels(side)[static_cast<std::size_t>(p)];
    index_.erase(queue.front().id);
    queue.pop_front();
    if (queue.empty()) {
      clear_bit(side, p);
      refresh_best(side, p);
    }
  }

  void insert(const Order& order) {
    levels(order.side)[static_cast<std::size_t>(order.price)].push_back(order);
    set_bit(order.side, order.price);
    index_.emplace(order.id, Location{order.side, order.price});
    if (order.side == Side::Buy) {
      if (best_bid_ == kNoPrice || order.price > best_bid_) best_bid_ = order.price;
    } else {
      if (best_ask_ == kNoPrice || order.price < best_ask_) best_ask_ = order.price;
    }
  }

  bool contains(OrderId id) const { return index_.count(id) != 0; }

  std::optional<Order> cancel(OrderId id) {
    const auto found = index_.find(id);
    if (found == index_.end()) return std::nullopt;
    const Location loc = found->second;
    auto& queue = levels(loc.side)[static_cast<std::size_t>(loc.price)];

    for (auto it = queue.begin(); it != queue.end(); ++it) {  // O(level size): v0's cost
      if (it->id != id) continue;
      const Order copy = *it;
      queue.erase(it);
      index_.erase(found);
      if (queue.empty()) {
        clear_bit(loc.side, loc.price);
        const Price best = (loc.side == Side::Buy) ? best_bid_ : best_ask_;
        if (loc.price == best) refresh_best(loc.side, loc.price);
      }
      return copy;
    }
    return std::nullopt;  // unreachable if the index is consistent
  }

  // ------------------------------------------------- not part of the contract
  std::size_t size() const { return index_.size(); }

  std::uint64_t total_remaining(Side side) const {
    std::uint64_t total = 0;
    for_each(side, [&](const Order& o) { total += o.remaining; });
    return total;
  }

  template <typename F>
  void walk(Side side, F&& fn) const {
    const auto& lv = (side == Side::Buy) ? bids_ : asks_;
    std::optional<Price> p = best(side);
    while (p) {
      for (const auto& order : lv[static_cast<std::size_t>(*p)])
        if (!fn(order)) return;
      p = next_level(side, *p);
    }
  }

  template <typename F>
  void for_each(Side side, F&& fn) const {
    walk(side, [&](const Order& o) { fn(o); return true; });
  }

 private:
  using Word = std::uint64_t;
  static constexpr Price kNoPrice = 0;  // kMinPrice is 1, so 0 is a safe sentinel
  static constexpr std::size_t kWords = (kPriceLevels + 63) / 64;
  using Bitmask = std::array<Word, kWords>;

  struct Location {
    Side side{};
    Price price{};
  };

  std::vector<std::deque<Order>>& levels(Side s) { return (s == Side::Buy) ? bids_ : asks_; }
  Bitmask& occ(Side s) { return (s == Side::Buy) ? bid_occ_ : ask_occ_; }
  const Bitmask& occ(Side s) const { return (s == Side::Buy) ? bid_occ_ : ask_occ_; }

  void set_bit(Side s, Price p) {
    occ(s)[static_cast<std::size_t>(p) >> 6] |= (Word{1} << (static_cast<unsigned>(p) & 63u));
  }
  void clear_bit(Side s, Price p) {
    occ(s)[static_cast<std::size_t>(p) >> 6] &= ~(Word{1} << (static_cast<unsigned>(p) & 63u));
  }

  // Lowest occupied level at or above `from`.
  static std::optional<Price> scan_up(const Bitmask& w, Price from) {
    if (from > kMaxPrice) return std::nullopt;
    std::size_t i = static_cast<std::size_t>(from) >> 6;
    Word bits = w[i] & (~Word{0} << (static_cast<unsigned>(from) & 63u));
    for (;;) {
      if (bits) return static_cast<Price>(i * 64 + static_cast<std::size_t>(std::countr_zero(bits)));
      if (++i >= kWords) return std::nullopt;
      bits = w[i];
    }
  }

  // Highest occupied level at or below `from`.
  static std::optional<Price> scan_down(const Bitmask& w, Price from) {
    if (from < kMinPrice) return std::nullopt;
    std::size_t i = static_cast<std::size_t>(from) >> 6;
    const unsigned shift = 63u - (static_cast<unsigned>(from) & 63u);
    Word bits = (w[i] << shift) >> shift;  // drop everything above `from`
    for (;;) {
      if (bits)
        return static_cast<Price>(i * 64 + 63 - static_cast<std::size_t>(std::countl_zero(bits)));
      if (i == 0) return std::nullopt;
      bits = w[--i];
    }
  }

  std::optional<Price> next_level(Side side, Price from) const {
    if (side == Side::Buy) {
      if (from <= kMinPrice) return std::nullopt;
      return scan_down(bid_occ_, from - 1);
    }
    if (from >= kMaxPrice) return std::nullopt;
    return scan_up(ask_occ_, from + 1);
  }

  // The best level just emptied. Find the next one outward, or report the side
  // as empty. This is the only path that touches the bitmask scan.
  void refresh_best(Side side, Price emptied) {
    const auto next = next_level(side, emptied);
    const Price value = next.value_or(kNoPrice);
    if (side == Side::Buy) best_bid_ = value;
    else best_ask_ = value;
  }

  std::vector<std::deque<Order>> bids_;
  std::vector<std::deque<Order>> asks_;
  Bitmask bid_occ_{};
  Bitmask ask_occ_{};
  Price best_bid_{kNoPrice};
  Price best_ask_{kNoPrice};
  std::unordered_map<OrderId, Location> index_{};
};

static_assert(Book<BookV1Ladder>, "BookV1Ladder must satisfy the Book contract");

}  // namespace pokex
