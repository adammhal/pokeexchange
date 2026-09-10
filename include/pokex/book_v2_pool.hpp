#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "pokex/book_concept.hpp"
#include "pokex/detail/ladder_pool.hpp"
#include "pokex/types.hpp"

namespace pokex {

// v2: intrusive lists over an object pool.
//
// The change from v1 is that a price level stops being a std::deque and becomes
// a doubly-linked list whose links live inside the order node itself, drawn from
// a pool allocated once up front. Two consequences:
//
//   1. No allocation on the hot path. In v1 every push_back could call into the
//      allocator, which may take a lock and may go to the OS. Here the free list
//      hands back a slot that is already warm.
//   2. An order and its links share a cache line, so following a level costs one
//      fetch per node instead of one for the node and one for the deque's
//      bookkeeping.
//
// Cancel is still NOT O(1), and that is deliberate. This version knows only the
// (side, price) an order lives at, so it still has to walk that level's list
// looking for the id. The walk is cheaper than v1's deque scan, but it is the
// same complexity. v3 is what fixes it.
class BookV2Pool {
 public:
  std::optional<Price> best(Side side) const { return core_.best(side); }

  Order& front(Side side) { return core_.node(core_.front_index(side)).order; }

  void pop_front(Side side) {
    const detail::NodeIndex idx = core_.front_index(side);
    index_.erase(core_.node(idx).order.id);
    core_.unlink(idx);
    core_.release(idx);
  }

  void insert(const Order& order) {
    const detail::NodeIndex idx = core_.acquire(order);
    core_.push_back(idx);
    index_.emplace(order.id, Location{order.side, order.price});
  }

  bool contains(OrderId id) const { return index_.count(id) != 0; }

  std::optional<Order> cancel(OrderId id) {
    const auto found = index_.find(id);
    if (found == index_.end()) return std::nullopt;
    const Location loc = found->second;

    // O(level size): we know where the level is, but not where in it.
    for (detail::NodeIndex i = core_.level(loc.side, loc.price).head; i != detail::kNil;
         i = core_.node(i).next) {
      if (core_.node(i).order.id != id) continue;
      const Order copy = core_.node(i).order;
      core_.unlink(i);
      core_.release(i);
      index_.erase(found);
      return copy;
    }
    return std::nullopt;  // unreachable if the index is consistent
  }

  // ------------------------------------------------- not part of the contract
  std::size_t size() const { return index_.size(); }

  std::uint64_t total_remaining(Side side) const {
    std::uint64_t total = 0;
    core_.for_each(side, [&](const Order& o) { total += o.remaining; });
    return total;
  }

  template <typename F>
  void walk(Side side, F&& fn) const {
    core_.walk(side, static_cast<F&&>(fn));
  }

  template <typename F>
  void for_each(Side side, F&& fn) const {
    core_.for_each(side, static_cast<F&&>(fn));
  }

 private:
  struct Location {
    Side side{};
    Price price{};
  };

  detail::LadderPool core_{};
  std::unordered_map<OrderId, Location> index_{};
};

static_assert(Book<BookV2Pool>, "BookV2Pool must satisfy the Book contract");

}  // namespace pokex
