#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "helpers.hpp"
#include "pokex/exchange.hpp"

using namespace pokex;
using namespace pokex::test;

// The exchange is a router, not a matching engine. MatchingEngine stays exactly
// what it was, one instrument and nothing else, which is also what the
// benchmark measures. Everything about instruments lives out here.

namespace {

// Collects events tagged with the instrument they came from.
template <typename BookT>
class Venue {
 public:
  explicit Venue(std::size_t instruments) : exchange_(instruments) {}

  void limit(InstrumentId inst, OrderId id, Side side, Price px, Quantity qty,
             ParticipantId who = kAnonymous) {
    send(NewOrder{id, side, OrderType::Limit, px, qty, TimeInForce::GoodTillCancel,
                  false, who, inst});
  }
  void market(InstrumentId inst, OrderId id, Side side, Quantity qty) {
    send(NewOrder{id, side, OrderType::Market, 0, qty, TimeInForce::GoodTillCancel,
                  false, kAnonymous, inst});
  }
  void cancel(InstrumentId inst, OrderId id) { send(CancelOrder{id, inst}); }
  void modify(InstrumentId inst, OrderId id, Price px, Quantity qty) {
    send(ModifyOrder{id, px, qty, inst});
  }

  void send(const Command& c) {
    exchange_.submit(c, [this](InstrumentId inst, const Event& e) {
      log_.push_back("@" + std::to_string(inst) + " " + render(e));
    });
  }

  const std::vector<std::string>& log() const { return log_; }
  void clear() { log_.clear(); }
  Exchange<BookT>& exchange() { return exchange_; }

 private:
  Exchange<BookT> exchange_;
  std::vector<std::string> log_{};
};

}  // namespace

TEMPLATE_TEST_CASE("orders on different instruments do not match each other",
                   "[exchange]", POKEX_ALL_BOOKS) {
  Venue<TestType> v(3);
  v.limit(0, 1, Side::Sell, 101, 50);
  v.clear();
  v.limit(1, 2, Side::Buy, 101, 50);  // same price, different instrument
  CHECK(v.log() == std::vector<std::string>{"@1 ACK 2 seq=1"});
  CHECK(v.exchange().engine(0).book().best(Side::Sell) == 101);
  CHECK(v.exchange().engine(1).book().best(Side::Buy) == 101);
}

TEMPLATE_TEST_CASE("orders on the same instrument match as usual", "[exchange]",
                   POKEX_ALL_BOOKS) {
  Venue<TestType> v(3);
  v.limit(2, 1, Side::Sell, 101, 50);
  v.clear();
  v.limit(2, 2, Side::Buy, 101, 50);
  CHECK(v.log() == std::vector<std::string>{"@2 ACK 2 seq=2",
                                            "@2 TRD maker=1 taker=2 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("each instrument keeps its own book", "[exchange]",
                   POKEX_ALL_BOOKS) {
  Venue<TestType> v(2);
  v.limit(0, 1, Side::Buy, 99, 10);
  v.limit(1, 2, Side::Buy, 4200, 10);
  CHECK(v.exchange().engine(0).book().best(Side::Buy) == 99);
  CHECK(v.exchange().engine(1).book().best(Side::Buy) == 4200);
}

TEMPLATE_TEST_CASE("sequence numbers run per instrument", "[exchange]",
                   POKEX_ALL_BOOKS) {
  // Time priority is only ever compared within a book, so a shared counter
  // would be a contention point that buys nothing.
  Venue<TestType> v(2);
  v.limit(0, 1, Side::Buy, 99, 10);
  v.limit(0, 2, Side::Buy, 98, 10);
  v.limit(1, 3, Side::Buy, 99, 10);
  CHECK(v.log() == std::vector<std::string>{"@0 ACK 1 seq=1", "@0 ACK 2 seq=2",
                                            "@1 ACK 3 seq=1"});
}

TEMPLATE_TEST_CASE("the same order id may be reused on another instrument",
                   "[exchange]", POKEX_ALL_BOOKS) {
  // Order ids are scoped to an instrument, so this is not a duplicate.
  Venue<TestType> v(2);
  v.limit(0, 1, Side::Buy, 99, 10);
  v.clear();
  v.limit(1, 1, Side::Buy, 99, 10);
  CHECK(v.log() == std::vector<std::string>{"@1 ACK 1 seq=1"});
}

TEMPLATE_TEST_CASE("a duplicate id on the same instrument is still rejected",
                   "[exchange]", POKEX_ALL_BOOKS) {
  Venue<TestType> v(2);
  v.limit(0, 1, Side::Buy, 99, 10);
  v.clear();
  v.limit(0, 1, Side::Buy, 98, 10);
  CHECK(v.log() == std::vector<std::string>{"@0 REJ 1 duplicate_order_id"});
}

TEMPLATE_TEST_CASE("an unknown instrument is rejected", "[exchange]",
                   POKEX_ALL_BOOKS) {
  Venue<TestType> v(2);
  v.limit(7, 1, Side::Buy, 99, 10);
  CHECK(v.log() == std::vector<std::string>{"@7 REJ 1 unknown_instrument"});
}

TEMPLATE_TEST_CASE("cancelling on the wrong instrument does not find the order",
                   "[exchange]", POKEX_ALL_BOOKS) {
  Venue<TestType> v(2);
  v.limit(0, 1, Side::Buy, 99, 10);
  v.clear();
  v.cancel(1, 1);
  CHECK(v.log() == std::vector<std::string>{"@1 REJ 1 unknown_order"});
  CHECK(v.exchange().engine(0).book().best(Side::Buy) == 99);  // still resting
}

TEMPLATE_TEST_CASE("cancelling on the right instrument works", "[exchange]",
                   POKEX_ALL_BOOKS) {
  Venue<TestType> v(2);
  v.limit(0, 1, Side::Buy, 99, 10);
  v.clear();
  v.cancel(0, 1);
  CHECK(v.log() == std::vector<std::string>{"@0 CXL 1 unfilled=10 user"});
  CHECK(v.exchange().engine(0).book().best(Side::Buy) == std::nullopt);
}

TEMPLATE_TEST_CASE("modify is routed to the right instrument", "[exchange]",
                   POKEX_ALL_BOOKS) {
  Venue<TestType> v(2);
  v.limit(0, 1, Side::Sell, 101, 100);
  v.limit(1, 1, Side::Sell, 101, 100);  // same id, other instrument
  v.clear();
  v.modify(1, 1, 101, 60);
  CHECK(v.log() == std::vector<std::string>{"@1 MOD 1 qty=60 px=101 seq=1 kept"});
  // Instrument 0's order is untouched at its original size.
  v.clear();
  v.limit(0, 2, Side::Buy, 101, 100);
  CHECK(v.log() == std::vector<std::string>{"@0 ACK 2 seq=2",
                                            "@0 TRD maker=1 taker=2 px=101 qty=100"});
}

TEMPLATE_TEST_CASE("a market order only sweeps its own instrument", "[exchange]",
                   POKEX_ALL_BOOKS) {
  Venue<TestType> v(2);
  v.limit(0, 1, Side::Sell, 101, 50);
  v.limit(1, 2, Side::Sell, 101, 50);
  v.clear();
  v.market(0, 3, Side::Buy, 200);
  CHECK(v.log() == std::vector<std::string>{"@0 ACK 3 seq=2",
                                            "@0 TRD maker=1 taker=3 px=101 qty=50",
                                            "@0 CXL 3 unfilled=150 no_liquidity"});
  CHECK(v.exchange().engine(1).book().best(Side::Sell) == 101);  // untouched
}
