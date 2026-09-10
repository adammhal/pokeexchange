#pragma once
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "pokex/events.hpp"
#include "pokex/types.hpp"

// Text encoding for the replay tool. Deliberately human-readable: a diff you
// can read is worth far more during development than a fast parser, and this
// lives outside the engine so it never touches a latency measurement.
//
//   commands                                    events
//   N <inst> <id> B|S L|M <px> <qty> [flags...]   A <inst> <id> <seq>
//   C <inst> <id>                                 R <inst> <id> <reason>
//   M <inst> <id> <px> <qty>                      T <inst> <maker> <taker> <px> <qty> <seq>
//                                                 M <inst> <id> <qty> <px> <seq> kept|lost
//                                                 X <inst> <id> <unfilled> <reason>
//
// Every line names its instrument, so a file is self-describing and a single
// recording can carry a whole venue.
//
// Flags are optional and order-independent, so files written before they
// existed still parse: GTC | IOC | FOK set the time in force, PO means
// post-only, and P:<n> attributes the order to a participant for self-trade
// prevention.
namespace pokex::codec {

struct ParseResult {
  enum class Status { Ok, Skip, Error } status{Status::Error};
  Command command{};
  std::string error{};

  static ParseResult ok(Command c) { return {Status::Ok, c, {}}; }
  static ParseResult skip() { return {Status::Skip, {}, {}}; }
  static ParseResult fail(std::string m) { return {Status::Error, {}, std::move(m)}; }
};

namespace detail {

inline std::string_view trim(std::string_view s) {
  const auto ws = " \t\r\n";
  const auto b = s.find_first_not_of(ws);
  if (b == std::string_view::npos) return {};
  return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

inline bool next_token(std::string_view& rest, std::string_view& out) {
  rest = trim(rest);
  if (rest.empty()) return false;
  const auto end = rest.find_first_of(" \t");
  out = rest.substr(0, end);
  rest = (end == std::string_view::npos) ? std::string_view{} : rest.substr(end);
  return true;
}

template <typename T>
bool to_number(std::string_view t, T& out) {
  const auto* first = t.data();
  const auto* last = t.data() + t.size();
  if (!t.empty() && t.front() == '+') ++first;
  const auto r = std::from_chars(first, last, out);
  return r.ec == std::errc{} && r.ptr == last;
}

}  // namespace detail

inline ParseResult parse_command(std::string_view line) {
  using detail::next_token;
  using detail::to_number;

  std::string_view rest = detail::trim(line);
  if (rest.empty() || rest.front() == '#') return ParseResult::skip();

  std::string_view kind;
  if (!next_token(rest, kind)) return ParseResult::skip();

  std::string_view inst_tok;
  InstrumentId instrument{};
  if (!next_token(rest, inst_tok) || !to_number(inst_tok, instrument))
    return ParseResult::fail("every command needs a numeric instrument id");

  if (kind == "C") {
    std::string_view id_tok;
    OrderId id{};
    if (!next_token(rest, id_tok) || !to_number(id_tok, id))
      return ParseResult::fail("cancel needs a numeric order id");
    return ParseResult::ok(CancelOrder{id, instrument});
  }

  if (kind == "M") {
    std::string_view id_tok, px_tok, qty_tok;
    ModifyOrder mod{};
    mod.instrument = instrument;
    if (!next_token(rest, id_tok) || !next_token(rest, px_tok) ||
        !next_token(rest, qty_tok))
      return ParseResult::fail("modify needs: id price qty");
    if (!to_number(id_tok, mod.id)) return ParseResult::fail("bad order id");
    if (!to_number(px_tok, mod.price)) return ParseResult::fail("bad price");
    if (!to_number(qty_tok, mod.quantity)) return ParseResult::fail("bad quantity");
    return ParseResult::ok(mod);
  }

  if (kind != "N") return ParseResult::fail("unknown command '" + std::string(kind) + "'");

  std::string_view id_tok, side_tok, type_tok, px_tok, qty_tok;
  if (!next_token(rest, id_tok) || !next_token(rest, side_tok) ||
      !next_token(rest, type_tok) || !next_token(rest, px_tok) ||
      !next_token(rest, qty_tok))
    return ParseResult::fail("new order needs: id side type price qty");

  NewOrder n{};
  n.instrument = instrument;
  if (!to_number(id_tok, n.id)) return ParseResult::fail("bad order id");
  if (side_tok == "B") n.side = Side::Buy;
  else if (side_tok == "S") n.side = Side::Sell;
  else return ParseResult::fail("side must be B or S");
  if (type_tok == "L") n.type = OrderType::Limit;
  else if (type_tok == "M") n.type = OrderType::Market;
  else return ParseResult::fail("type must be L or M");
  if (!to_number(px_tok, n.price)) return ParseResult::fail("bad price");
  if (!to_number(qty_tok, n.quantity)) return ParseResult::fail("bad quantity");

  std::string_view flag;
  while (next_token(rest, flag)) {
    if (flag == "GTC") n.tif = TimeInForce::GoodTillCancel;
    else if (flag == "IOC") n.tif = TimeInForce::ImmediateOrCancel;
    else if (flag == "FOK") n.tif = TimeInForce::FillOrKill;
    else if (flag == "PO") n.post_only = true;
    else if (flag.size() > 2 && flag.substr(0, 2) == "P:") {
      if (!to_number(flag.substr(2), n.participant))
        return ParseResult::fail("bad participant id");
    } else {
      return ParseResult::fail("unknown order flag '" + std::string(flag) + "'");
    }
  }
  return ParseResult::ok(n);
}

inline std::string reason_name(RejectReason r) {
  switch (r) {
    case RejectReason::ZeroQuantity: return "zero_quantity";
    case RejectReason::DuplicateOrderId: return "duplicate_order_id";
    case RejectReason::UnknownOrder: return "unknown_order";
    case RejectReason::PriceOutOfRange: return "price_out_of_range";
    case RejectReason::PostOnlyWouldCross: return "post_only_would_cross";
    case RejectReason::PostOnlyMarketOrder: return "post_only_market";
    case RejectReason::UnknownInstrument: return "unknown_instrument";
  }
  return "unknown";
}

inline std::string cancel_reason_name(CancelReason r) {
  switch (r) {
    case CancelReason::UserRequested: return "user";
    case CancelReason::NoLiquidity: return "no_liquidity";
    case CancelReason::FillOrKillUnfillable: return "fok_unfillable";
    case CancelReason::SelfTradePrevented: return "self_trade";
  }
  return "unknown";
}

inline std::string format_event(const Event& e, InstrumentId instrument) {
  const std::string inst = std::to_string(instrument) + " ";
  return std::visit(
      [&inst](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Accepted>) {
          return "A " + inst + std::to_string(v.id) + " " + std::to_string(v.seq);
        } else if constexpr (std::is_same_v<T, Rejected>) {
          return "R " + inst + std::to_string(v.id) + " " + reason_name(v.reason);
        } else if constexpr (std::is_same_v<T, Trade>) {
          return "T " + inst + std::to_string(v.maker_id) + " " + std::to_string(v.taker_id) +
                 " " + std::to_string(v.price) + " " + std::to_string(v.quantity) +
                 " " + std::to_string(v.seq);
        } else if constexpr (std::is_same_v<T, Modified>) {
          return "M " + inst + std::to_string(v.id) + " " + std::to_string(v.quantity) + " " +
                 std::to_string(v.price) + " " + std::to_string(v.seq) +
                 (v.kept_priority ? " kept" : " lost");
        } else {
          return "X " + inst + std::to_string(v.id) + " " + std::to_string(v.unfilled_quantity) +
                 " " + cancel_reason_name(v.reason);
        }
      },
      e);
}

}  // namespace pokex::codec
