#pragma once
#include <concepts>
#include <optional>

#include "pokex/types.hpp"

namespace pokex {

// The contract every book version must satisfy.
//
// The matching loop in matching.hpp is written ONCE against this concept, and
// v0..v3 are five different attempts to make these operations faster. The seam
// is a compile-time template rather than a virtual base class on purpose: a
// virtual call is an indirect jump that defeats inlining and pollutes the
// branch predictor, so using one here would mean benchmarking our own
// abstraction instead of the data structure.
//
// Note the contract exposes operations rather than raw handles. A handle into a
// std::deque would dangle the moment an order in the middle was cancelled, so
// exposing one would make v0 unimplementable without undefined behaviour.
template <typename B>
concept Book = requires(B book, const B const_book, Side side, const Order& order,
                        OrderId id) {
  // Best price on a side, or nullopt if that side is empty. For bids this is
  // the highest price; for asks the lowest.
  { const_book.best(side) } -> std::same_as<std::optional<Price>>;

  // Oldest order at the best price on a side. Precondition: best(side) has a
  // value. Returned by reference so a partial fill can decrement it in place.
  { book.front(side) } -> std::same_as<Order&>;

  // Remove the oldest order at the best price on a side, dropping the level if
  // it becomes empty. Precondition: best(side) has a value.
  { book.pop_front(side) } -> std::same_as<void>;

  // Rest an order at the back of its price level. Precondition: !contains(id).
  { book.insert(order) } -> std::same_as<void>;

  { const_book.contains(id) } -> std::same_as<bool>;

  // Remove by id, returning the order as it stood. nullopt if not present.
  { book.cancel(id) } -> std::same_as<std::optional<Order>>;

  // Locate a resting order for modification in place, or nullptr.
  //
  // Returning a pointer rather than a copy is what makes a quantity reduction
  // possible at all: the order has to be changed where it sits, because the
  // whole point is that it does not lose its place in the queue. There is no
  // "insert at this position" operation and there should not be.
  { book.find(id) } -> std::same_as<Order*>;

  // Visit resting orders on a side in strict priority order: best price first,
  // and oldest first within a price. The callback returns false to stop.
  //
  // Fill-or-kill needs this. It has to know whether the whole quantity is
  // available BEFORE printing any trade, because discovering it mid-match would
  // mean unwinding fills, and an engine that can unwind a fill has no audit
  // trail worth the name. The early exit matters because a deep book can hold
  // millions of orders and the answer is usually known within a few.
  { const_book.walk(side, [](const Order&) { return true; }) };
};

}  // namespace pokex
