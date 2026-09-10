#pragma once
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "pokex/all_books.hpp"
#include "pokex/events.hpp"
#include "pokex/matching.hpp"

namespace pokex::test {

// Renders an event as one compact line, so tests can assert on a readable
// transcript rather than poking at variant members.
inline std::string render(const Event& e) {
  std::ostringstream o;
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Accepted>) {
          o << "ACK " << v.id << " seq=" << v.seq;
        } else if constexpr (std::is_same_v<T, Rejected>) {
          o << "REJ " << v.id << " ";
          switch (v.reason) {
            case RejectReason::ZeroQuantity: o << "zero_quantity"; break;
            case RejectReason::DuplicateOrderId: o << "duplicate_order_id"; break;
            case RejectReason::UnknownOrder: o << "unknown_order"; break;
            case RejectReason::PriceOutOfRange: o << "price_out_of_range"; break;
            case RejectReason::PostOnlyWouldCross: o << "post_only_would_cross"; break;
            case RejectReason::PostOnlyMarketOrder: o << "post_only_market"; break;
          }
        } else if constexpr (std::is_same_v<T, Trade>) {
          o << "TRD maker=" << v.maker_id << " taker=" << v.taker_id
            << " px=" << v.price << " qty=" << v.quantity;
        } else {
          o << "CXL " << v.id << " unfilled=" << v.unfilled_quantity << " ";
          switch (v.reason) {
            case CancelReason::UserRequested: o << "user"; break;
            case CancelReason::NoLiquidity: o << "no_liquidity"; break;
            case CancelReason::FillOrKillUnfillable: o << "fok_unfillable"; break;
          }
        }
      },
      e);
  return o.str();
}

// Every book version, for TEMPLATE_TEST_CASE. Adding a version here
// automatically subjects it to the entire suite: golden, property and fuzz.
#define POKEX_ALL_BOOKS \
  pokex::BookV0Map, pokex::BookV1Ladder, pokex::BookV2Pool, pokex::BookV3Hash

// A harness that drives the engine and collects the event transcript.
// Generic over the book, so one suite covers every version.
template <typename BookT>
class Harness {
 public:
  void limit(OrderId id, Side side, Price px, Quantity qty) {
    send(NewOrder{id, side, OrderType::Limit, px, qty});
  }
  void market(OrderId id, Side side, Quantity qty) {
    send(NewOrder{id, side, OrderType::Market, 0, qty});
  }
  void ioc(OrderId id, Side side, Price px, Quantity qty) {
    send(NewOrder{id, side, OrderType::Limit, px, qty, TimeInForce::ImmediateOrCancel});
  }
  void fok(OrderId id, Side side, Price px, Quantity qty) {
    send(NewOrder{id, side, OrderType::Limit, px, qty, TimeInForce::FillOrKill});
  }
  void market_fok(OrderId id, Side side, Quantity qty) {
    send(NewOrder{id, side, OrderType::Market, 0, qty, TimeInForce::FillOrKill});
  }
  void post_only(OrderId id, Side side, Price px, Quantity qty) {
    send(NewOrder{id, side, OrderType::Limit, px, qty, TimeInForce::GoodTillCancel, true});
  }
  void post_only_market(OrderId id, Side side, Quantity qty) {
    send(NewOrder{id, side, OrderType::Market, 0, qty, TimeInForce::GoodTillCancel, true});
  }
  void cancel(OrderId id) { send(CancelOrder{id}); }

  void send(const Command& c) {
    engine_.submit(c, [&](const Event& e) { events_.push_back(e); });
  }

  // Transcript since the last clear, one string per event.
  std::vector<std::string> log() const {
    std::vector<std::string> out;
    for (const auto& e : events_) out.push_back(render(e));
    return out;
  }
  void clear() { events_.clear(); }

  std::vector<std::string> trades() const {
    std::vector<std::string> out;
    for (const auto& e : events_)
      if (std::holds_alternative<Trade>(e)) out.push_back(render(e));
    return out;
  }

  MatchingEngine<BookT>& engine() { return engine_; }

 private:
  MatchingEngine<BookT> engine_{};
  std::vector<Event> events_{};
};

}  // namespace pokex::test
