#pragma once
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

#include "pokex/book_concept.hpp"
#include "pokex/types.hpp"

namespace pokex {

// v0: the honest baseline. A std::map of price levels, each a std::deque used as
// a FIFO queue. Correct, simple, and slow.
//
// It is slow in two specific ways, and both are the point of keeping it:
//   1. Every level is a separately allocated tree node, so walking the book is a
//      pointer chase with a likely cache miss per hop.
//   2. cancel() has to linear-scan the deque at the order's price. Cancels are
//      roughly 90% of real message traffic, so this is the worst possible place
//      to be linear.
//
// v1..v3 attack exactly these. This class is never deleted, because the
// comparison is the story.
class BookV0Map {
 public:
  std::optional<Price> best(Side side) const {
    if (side == Side::Buy) {
      if (bids_.empty()) return std::nullopt;
      return std::prev(bids_.end())->first;  // highest bid
    }
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;  // lowest ask
  }

  Order& front(Side side) { return best_level(side).front(); }

  void pop_front(Side side) {
    auto& levels = (side == Side::Buy) ? bids_ : asks_;
    auto it = (side == Side::Buy) ? std::prev(levels.end()) : levels.begin();
    index_.erase(it->second.front().id);
    it->second.pop_front();
    if (it->second.empty()) levels.erase(it);
  }

  void insert(const Order& order) {
    auto& levels = (order.side == Side::Buy) ? bids_ : asks_;
    levels[order.price].push_back(order);
    index_.emplace(order.id, Location{order.side, order.price});
  }

  bool contains(OrderId id) const { return index_.count(id) != 0; }

  std::optional<Order> cancel(OrderId id) {
    const auto found = index_.find(id);
    if (found == index_.end()) return std::nullopt;
    const Location loc = found->second;
    auto& levels = (loc.side == Side::Buy) ? bids_ : asks_;
    const auto level = levels.find(loc.price);
    if (level == levels.end()) return std::nullopt;  // unreachable if consistent

    auto& queue = level->second;
    for (auto it = queue.begin(); it != queue.end(); ++it) {  // O(level size): v0
      if (it->id != id) continue;
      const Order copy = *it;
      queue.erase(it);
      if (queue.empty()) levels.erase(level);
      index_.erase(found);
      return copy;
    }
    return std::nullopt;  // unreachable if consistent
  }

  // ------------------------------------------------- not part of the contract,
  // used by the property tests to check invariants from the outside.
  std::size_t size() const { return index_.size(); }

  std::uint64_t total_remaining(Side side) const {
    std::uint64_t total = 0;
    for (const auto& [price, queue] : (side == Side::Buy) ? bids_ : asks_)
      for (const auto& order : queue) total += order.remaining;
    return total;
  }

  // Visits resting orders on a side in strict priority order. The callback
  // returns false to stop.
  template <typename F>
  void walk(Side side, F&& fn) const {
    if (side == Side::Buy) {
      for (auto it = bids_.rbegin(); it != bids_.rend(); ++it)
        for (const auto& order : it->second)
          if (!fn(order)) return;
    } else {
      for (const auto& [price, queue] : asks_)
        for (const auto& order : queue)
          if (!fn(order)) return;
    }
  }

  template <typename F>
  void for_each(Side side, F&& fn) const {
    walk(side, [&](const Order& o) { fn(o); return true; });
  }

 private:
  struct Location {
    Side side{};
    Price price{};
  };

  std::deque<Order>& best_level(Side side) {
    return (side == Side::Buy) ? std::prev(bids_.end())->second
                               : asks_.begin()->second;
  }

  std::map<Price, std::deque<Order>> bids_{};
  std::map<Price, std::deque<Order>> asks_{};
  std::unordered_map<OrderId, Location> index_{};
};

static_assert(Book<BookV0Map>, "BookV0Map must satisfy the Book contract");

}  // namespace pokex
