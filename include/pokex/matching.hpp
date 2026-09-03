#pragma once
#include <algorithm>
#include <variant>

#include "pokex/book_concept.hpp"
#include "pokex/events.hpp"
#include "pokex/types.hpp"

namespace pokex {

// The matching engine. Written once against the Book concept; v0..v3 plug in
// underneath without a line of this file changing.
//
// Pure by construction: no I/O, no threads, no clock, no randomness. Time is the
// sequence counter below, which is what makes replay byte-identical.
template <Book B>
class MatchingEngine {
 public:
  // Consumes one command, emitting zero or more events through `emit`.
  // Never throws. Bad input produces a Rejected event and leaves the book
  // untouched, because a half-updated book is unrecoverable.
  template <typename Emit>
  void submit(const Command& command, Emit&& emit) {
    std::visit([&](const auto& c) { handle(c, emit); }, command);
  }

  const B& book() const { return book_; }
  Sequence sequence() const { return seq_; }

 private:
  template <typename Emit>
  void handle(const NewOrder& n, Emit& emit) {
    if (n.quantity == 0) {
      emit(Event{Rejected{n.id, RejectReason::ZeroQuantity}});
      return;
    }
    // Only limit orders carry a meaningful price. A market order's price field
    // is meaningless (and is conventionally 0, which is out of range), so range
    // checking it would reject every market order ever sent.
    if (n.type == OrderType::Limit && (n.price < kMinPrice || n.price > kMaxPrice)) {
      emit(Event{Rejected{n.id, RejectReason::PriceOutOfRange}});
      return;
    }
    if (book_.contains(n.id)) {
      emit(Event{Rejected{n.id, RejectReason::DuplicateOrderId}});
      return;
    }

    const Sequence seq = ++seq_;
    emit(Event{Accepted{n.id, seq}});

    Order order{n.id, n.side, n.type, n.price, n.quantity, n.quantity, seq};
    const Side other = opposite(n.side);

    while (order.remaining > 0) {
      const auto best = book_.best(other);
      if (!best || !crosses(order, *best)) break;

      Order& resting = book_.front(other);  // oldest at the best price
      const Quantity fill = std::min(order.remaining, resting.remaining);

      emit(Event{Trade{resting.id, order.id, *best, fill, seq}});
      order.remaining -= fill;
      resting.remaining -= fill;

      // NOTE: pop_front invalidates `resting`. Nothing reads it afterwards.
      if (resting.remaining == 0) book_.pop_front(other);
    }

    if (order.remaining == 0) return;
    if (order.type == OrderType::Limit) {
      book_.insert(order);  // join the back of the queue and wait
    } else {
      emit(Event{Canceled{order.id, order.remaining}});  // nothing left to hit
    }
  }

  template <typename Emit>
  void handle(const CancelOrder& c, Emit& emit) {
    auto removed = book_.cancel(c.id);
    if (!removed) {
      emit(Event{Rejected{c.id, RejectReason::UnknownOrder}});
      return;
    }
    ++seq_;
    emit(Event{Canceled{c.id, removed->remaining}});
  }

  // Would this incoming order accept a trade at `best_other`?
  // A market order has no price limit, so its price field is never read.
  static constexpr bool crosses(const Order& incoming, Price best_other) {
    if (incoming.type == OrderType::Market) return true;
    return incoming.side == Side::Buy ? best_other <= incoming.price
                                     : best_other >= incoming.price;
  }

  B book_{};
  Sequence seq_{0};
};

}  // namespace pokex
