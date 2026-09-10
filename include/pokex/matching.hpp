#pragma once
#include <algorithm>
#include <cstdint>
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
    // Validate the message's own fields first, then the state-dependent checks.
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
    // An order that must not trade, at no particular price, can do nothing.
    if (n.post_only && n.type == OrderType::Market) {
      emit(Event{Rejected{n.id, RejectReason::PostOnlyMarketOrder}});
      return;
    }
    if (book_.contains(n.id)) {
      emit(Event{Rejected{n.id, RejectReason::DuplicateOrderId}});
      return;
    }

    // A probe carrying the terms but no sequence number yet, for the pre-trade
    // policy checks below. Neither of them may print a trade or touch the book.
    const Order probe{n.id, n.side, n.type, n.price, n.quantity, n.quantity, 0};
    const Side other = opposite(n.side);

    // Post-only is refused rather than executed, so it is settled before the
    // order is accepted and before it consumes a sequence number.
    if (n.post_only) {
      const auto best = book_.best(other);
      if (best && crosses(probe, *best)) {
        emit(Event{Rejected{n.id, RejectReason::PostOnlyWouldCross}});
        return;
      }
    }

    // Fill or kill is all or nothing, so feasibility has to be decided before
    // any trade prints. Discovering it half way through would mean unwinding
    // fills, and an engine that can unwind a fill has no audit trail worth the
    // name. It is accepted first, because the order was well formed; it simply
    // could not be filled.
    if (n.tif == TimeInForce::FillOrKill && available(other, probe) < n.quantity) {
      emit(Event{Accepted{n.id, ++seq_}});
      emit(Event{Canceled{n.id, n.quantity, CancelReason::FillOrKillUnfillable}});
      return;
    }

    const Sequence seq = ++seq_;
    emit(Event{Accepted{n.id, seq}});

    Order order{n.id, n.side, n.type, n.price, n.quantity, n.quantity, seq};
    match(order, n.tif, emit);
  }

  // Runs an order against the book, then decides what to do with any
  // remainder. Shared by new orders and by repriced ones, so the two cannot
  // disagree about how matching works.
  template <typename Emit>
  void match(Order& order, TimeInForce tif, Emit& emit) {
    const Side other = opposite(order.side);

    while (order.remaining > 0) {
      const auto best = book_.best(other);
      if (!best || !crosses(order, *best)) break;

      Order& resting = book_.front(other);  // oldest at the best price
      const Quantity fill = std::min(order.remaining, resting.remaining);

      emit(Event{Trade{resting.id, order.id, *best, fill, order.seq}});
      order.remaining -= fill;
      resting.remaining -= fill;

      // NOTE: pop_front invalidates `resting`. Nothing reads it afterwards.
      if (resting.remaining == 0) book_.pop_front(other);
    }

    if (order.remaining == 0) return;

    // Only a good-till-cancel limit order rests. Everything else gives up its
    // remainder here.
    if (order.type == OrderType::Limit && tif == TimeInForce::GoodTillCancel) {
      book_.insert(order);  // join the back of the queue and wait
    } else {
      emit(Event{Canceled{order.id, order.remaining, CancelReason::NoLiquidity}});
    }
  }

  // How much of `incoming` could be filled right now, capped at what it wants.
  // Walks the opposite side in priority order and stops at the first level the
  // order would not accept, since everything past it is worse.
  Quantity available(Side side, const Order& incoming) const {
    const Quantity needed = incoming.remaining;
    std::uint64_t total = 0;
    book_.walk(side, [&](const Order& resting) {
      if (!crosses(incoming, resting.price)) return false;
      total += resting.remaining;
      return total < needed;
    });
    return static_cast<Quantity>(std::min<std::uint64_t>(total, needed));
  }

  // Modify.
  //
  // The rule: an order keeps its place in the queue exactly as long as it has
  // not increased the risk anyone else is taking on its behalf. Reducing
  // quantity lowers your own exposure and harms nobody behind you, so there is
  // nothing to charge for. Asking for more inserts size that never waited, and
  // moving price means arriving somewhere you have never queued at all. Both
  // go to the back.
  //
  // Losing priority is implemented as a cancel plus a fresh insert, in terms of
  // primitives that already exist, rather than as a third code path that could
  // drift out of agreement with them.
  template <typename Emit>
  void handle(const ModifyOrder& m, Emit& emit) {
    if (m.quantity == 0) {
      emit(Event{Rejected{m.id, RejectReason::ZeroQuantity}});
      return;
    }
    if (m.price < kMinPrice || m.price > kMaxPrice) {
      emit(Event{Rejected{m.id, RejectReason::PriceOutOfRange}});
      return;
    }

    Order* resting = book_.find(m.id);
    if (resting == nullptr) {
      emit(Event{Rejected{m.id, RejectReason::UnknownOrder}});
      return;
    }

    // `quantity` is the new total, so what is still workable is the new total
    // less whatever has already been filled.
    const Quantity filled = resting->quantity - resting->remaining;

    if (m.quantity <= filled) {
      // The order is already done at least as much as it now asks for, so
      // there is nothing left to work.
      const Quantity unfilled = resting->remaining;
      book_.cancel(m.id);
      ++seq_;
      emit(Event{Canceled{m.id, unfilled, CancelReason::UserRequested}});
      return;
    }

    const Quantity new_remaining = m.quantity - filled;
    const bool same_price = (m.price == resting->price);
    const bool not_growing = (new_remaining <= resting->remaining);

    if (same_price && not_growing) {
      resting->quantity = m.quantity;
      resting->remaining = new_remaining;
      const Sequence kept = resting->seq;  // unchanged: that is the whole point
      ++seq_;
      emit(Event{Modified{m.id, m.quantity, m.price, kept, true}});
      return;
    }

    const Side side = resting->side;
    book_.cancel(m.id);

    const Sequence seq = ++seq_;
    emit(Event{Modified{m.id, m.quantity, m.price, seq, false}});

    // A resting order is always a good-till-cancel limit order, and a repriced
    // one may now cross, in which case it trades like any other aggressor.
    Order moved{m.id, side, OrderType::Limit, m.price, m.quantity, new_remaining, seq};
    match(moved, TimeInForce::GoodTillCancel, emit);
  }

  template <typename Emit>
  void handle(const CancelOrder& c, Emit& emit) {
    auto removed = book_.cancel(c.id);
    if (!removed) {
      emit(Event{Rejected{c.id, RejectReason::UnknownOrder}});
      return;
    }
    ++seq_;
    emit(Event{Canceled{c.id, removed->remaining, CancelReason::UserRequested}});
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
