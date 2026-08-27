#pragma once
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "pokex/book_v0_map.hpp"
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
          }
        } else if constexpr (std::is_same_v<T, Trade>) {
          o << "TRD maker=" << v.maker_id << " taker=" << v.taker_id
            << " px=" << v.price << " qty=" << v.quantity;
        } else {
          o << "CXL " << v.id << " unfilled=" << v.unfilled_quantity;
        }
      },
      e);
  return o.str();
}

// A harness that drives the engine and collects the event transcript.
class Harness {
 public:
  void limit(OrderId id, Side side, Price px, Quantity qty) {
    send(NewOrder{id, side, OrderType::Limit, px, qty});
  }
  void market(OrderId id, Side side, Quantity qty) {
    send(NewOrder{id, side, OrderType::Market, 0, qty});
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

  MatchingEngine<BookV0Map>& engine() { return engine_; }

 private:
  MatchingEngine<BookV0Map> engine_{};
  std::vector<Event> events_{};
};

}  // namespace pokex::test
