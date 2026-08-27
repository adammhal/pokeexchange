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
};

using Event = std::variant<Accepted, Rejected, Trade, Canceled>;

}  // namespace pokex
