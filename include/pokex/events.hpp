#pragma once
#include <cstdint>
#include <variant>

#include "pokex/types.hpp"

namespace pokex {

// ----------------------------------------------------------------- commands in
struct NewOrder {
  OrderId id{};
  Side side{};
  OrderType type{};
  Price price{};
  Quantity quantity{};
  TimeInForce tif{TimeInForce::GoodTillCancel};
  // Must rest. If it would trade on arrival it is rejected instead, which is
  // how a participant guarantees it will be the maker and never the taker.
  bool post_only{false};
};

struct CancelOrder {
  OrderId id{};
};

using Command = std::variant<NewOrder, CancelOrder>;

// ---------------------------------------------------------------- events out
enum class RejectReason : std::uint8_t {
  ZeroQuantity,
  DuplicateOrderId,
  UnknownOrder,
  PriceOutOfRange,
  PostOnlyWouldCross,   // it would have traded, so it is refused
  PostOnlyMarketOrder,  // a market order that must not trade is a contradiction
};

// Why an order stopped being live. Without this, "cancelled" conflates a
// participant withdrawing an order with the engine dropping a remainder it
// could not fill, and an audit trail that cannot tell those apart is not much
// of an audit trail.
enum class CancelReason : std::uint8_t {
  UserRequested,
  NoLiquidity,           // a market or IOC remainder with nothing left to hit
  FillOrKillUnfillable,  // could not be filled in its entirety
};

struct Accepted {
  OrderId id{};
  Sequence seq{};
};

struct Rejected {
  OrderId id{};
  RejectReason reason{};
};

// A trade always prints at the MAKER's price. The resting order published its
// terms and waited; the aggressor read them and accepted.
struct Trade {
  OrderId maker_id{};
  OrderId taker_id{};
  Price price{};
  Quantity quantity{};
  Sequence seq{};
};

struct Canceled {
  OrderId id{};
  Quantity unfilled_quantity{};
  CancelReason reason{CancelReason::UserRequested};
};

using Event = std::variant<Accepted, Rejected, Trade, Canceled>;

}  // namespace pokex
