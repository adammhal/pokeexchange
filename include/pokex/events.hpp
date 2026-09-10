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
  ParticipantId participant{kAnonymous};
  InstrumentId instrument{0};
};

struct CancelOrder {
  OrderId id{};
  InstrumentId instrument{0};
};

// Change a resting order's price or quantity.
//
// `quantity` is the new TOTAL for the order, including whatever has already
// been filled, which is the FIX convention. Setting it at or below the filled
// amount means there is no work left, so the order is cancelled.
struct ModifyOrder {
  OrderId id{};
  Price price{};
  Quantity quantity{};
  InstrumentId instrument{0};
};

using Command = std::variant<NewOrder, CancelOrder, ModifyOrder>;

// ---------------------------------------------------------------- events out
enum class RejectReason : std::uint8_t {
  ZeroQuantity,
  DuplicateOrderId,
  UnknownOrder,
  PriceOutOfRange,
  PostOnlyWouldCross,   // it would have traded, so it is refused
  PostOnlyMarketOrder,  // a market order that must not trade is a contradiction
  UnknownInstrument,
};

// Why an order stopped being live. Without this, "cancelled" conflates a
// participant withdrawing an order with the engine dropping a remainder it
// could not fill, and an audit trail that cannot tell those apart is not much
// of an audit trail.
enum class CancelReason : std::uint8_t {
  UserRequested,
  NoLiquidity,           // a market or IOC remainder with nothing left to hit
  FillOrKillUnfillable,  // could not be filled in its entirety
  SelfTradePrevented,    // it reached one of its own resting orders
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

// A modify was applied. `kept_priority` is the interesting part: it says
// whether the order held its place in the queue, and `seq` is its priority
// sequence afterwards, which is a new one if it went to the back.
struct Modified {
  OrderId id{};
  Quantity quantity{};
  Price price{};
  Sequence seq{};
  bool kept_priority{};
};

struct Canceled {
  OrderId id{};
  Quantity unfilled_quantity{};
  CancelReason reason{CancelReason::UserRequested};
};

using Event = std::variant<Accepted, Rejected, Trade, Modified, Canceled>;

}  // namespace pokex
