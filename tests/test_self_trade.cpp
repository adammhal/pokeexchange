#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace pokex;
using namespace pokex::test;

// Nothing about matching stops your own buy order from hitting your own sell
// order. The trade is economically meaningless, since you pay yourself, but it
// is not harmless: it prints on the public tape and so manufactures activity
// that never happened. Done deliberately and repeatedly that is wash trading,
// which is illegal market manipulation precisely because it fakes the volume
// signal other participants rely on.
//
// The policy here is to cancel the incoming order and leave the resting one
// alone. It is the simplest rule to reason about, and it has one consequence
// worth knowing and testing: your own resting order blocks you from reaching
// anything behind it.

namespace {
constexpr ParticipantId kAlice = 7;
constexpr ParticipantId kBob = 9;
}  // namespace

TEMPLATE_TEST_CASE("an order does not trade against its own participant",
                   "[book][stp]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50, kAlice);
  h.clear();
  h.limit(2, Side::Buy, 101, 50, kAlice);
  CHECK(h.trades().empty());
  CHECK(h.log() == std::vector<std::string>{"ACK 2 seq=2",
                                            "CXL 2 unfilled=50 self_trade"});
}

TEMPLATE_TEST_CASE("the resting order survives self-trade prevention",
                   "[book][stp]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50, kAlice);
  h.limit(2, Side::Buy, 101, 50, kAlice);  // cancelled by prevention
  h.clear();
  h.limit(3, Side::Buy, 101, 50, kBob);    // someone else can still take it
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=3 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("different participants trade normally", "[book][stp]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50, kAlice);
  h.clear();
  h.limit(2, Side::Buy, 101, 50, kBob);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=2 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("unattributed orders are not subject to prevention",
                   "[book][stp]", POKEX_ALL_BOOKS) {
  // Participant zero means the order carries no self-match identifier, and
  // prevention does not apply. Two such orders trade freely.
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.clear();
  h.limit(2, Side::Buy, 101, 50);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=2 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("fills against others are kept when prevention stops the rest",
                   "[book][stp]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 30, kBob);    // best ask, someone else's
  h.limit(2, Side::Sell, 102, 40, kAlice);  // behind it, Alice's own
  h.clear();
  h.limit(3, Side::Buy, 105, 100, kAlice);
  // Trades with Bob, then stops on reaching her own order.
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=3 px=101 qty=30"});
  CHECK(h.log().back() == "CXL 3 unfilled=70 self_trade");
}

TEMPLATE_TEST_CASE("a participant's own resting order blocks what is behind it",
                   "[book][stp]", POKEX_ALL_BOOKS) {
  // A direct consequence of cancelling the incoming order rather than skipping
  // past. Worth pinning down so the behaviour is deliberate rather than
  // discovered later.
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 10, kAlice);  // Alice's own, at the front
  h.limit(2, Side::Sell, 102, 90, kBob);    // Bob's, behind it
  h.clear();
  h.limit(3, Side::Buy, 105, 100, kAlice);
  CHECK(h.trades().empty());  // never reaches Bob's order
  CHECK(h.log().back() == "CXL 3 unfilled=100 self_trade");
}

TEMPLATE_TEST_CASE("fill-or-kill does not count liquidity prevention would block",
                   "[book][stp][fok]", POKEX_ALL_BOOKS) {
  // The important interaction. If the feasibility check ignored prevention it
  // would judge this order fillable, accept it, then stop half way and leave a
  // partial fill, which is exactly what fill-or-kill promises never to do.
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 40, kBob);    // reachable
  h.limit(2, Side::Sell, 102, 60, kAlice);  // Alice's own: unreachable to her
  h.clear();
  h.fok(3, Side::Buy, 105, 100, kAlice);    // needs all 100
  CHECK(h.trades().empty());                // and takes none of Bob's 40
  CHECK(h.log() == std::vector<std::string>{"ACK 3 seq=3",
                                            "CXL 3 unfilled=100 fok_unfillable"});
  CHECK(h.engine().book().best(Side::Sell) == 101);  // book untouched
}

TEMPLATE_TEST_CASE("fill-or-kill succeeds when prevention is not in the way",
                   "[book][stp][fok]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 40, kBob);
  h.limit(2, Side::Sell, 102, 60, kBob);
  h.clear();
  h.fok(3, Side::Buy, 105, 100, kAlice);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=3 px=101 qty=40",
                                               "TRD maker=2 taker=3 px=102 qty=60"});
}

TEMPLATE_TEST_CASE("a modify that reprices onto a participant's own order is prevented",
                   "[book][stp][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50, kAlice);
  h.limit(2, Side::Buy, 95, 50, kAlice);
  h.clear();
  h.modify(2, 101, 50);  // Alice's bid moves onto Alice's own ask
  CHECK(h.trades().empty());
  CHECK(h.log().back() == "CXL 2 unfilled=50 self_trade");
  CHECK(h.engine().book().best(Side::Sell) == 101);  // her ask still stands
}

TEMPLATE_TEST_CASE("a market order stops at its own resting order", "[book][stp]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50, kAlice);
  h.clear();
  h.market(2, Side::Buy, 200, kAlice);
  CHECK(h.trades().empty());
  CHECK(h.log().back() == "CXL 2 unfilled=200 self_trade");
}
