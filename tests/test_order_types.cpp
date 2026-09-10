#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace pokex;
using namespace pokex::test;

// ─────────────────────────────────────────────────────── immediate or cancel
//
// "Take what is there, but do not leave me exposed." A resting order broadcasts
// your intentions to everyone; IOC lets you take liquidity without ever showing
// your hand.

TEMPLATE_TEST_CASE("an IOC order fills what it can and cancels the rest",
                   "[book][ioc]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.clear();
  h.ioc(2, Side::Buy, 101, 130);
  CHECK(h.log() == std::vector<std::string>{"ACK 2 seq=2",
                                            "TRD maker=1 taker=2 px=101 qty=50",
                                            "CXL 2 unfilled=80 no_liquidity"});
}

TEMPLATE_TEST_CASE("an IOC order never rests", "[book][ioc]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.ioc(1, Side::Buy, 99, 100);
  CHECK(h.engine().book().best(Side::Buy) == std::nullopt);
  CHECK(h.log() == std::vector<std::string>{"ACK 1 seq=1",
                                            "CXL 1 unfilled=100 no_liquidity"});
}

TEMPLATE_TEST_CASE("an IOC order that fills completely reports no cancel",
                   "[book][ioc]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 200);
  h.clear();
  h.ioc(2, Side::Buy, 101, 50);
  CHECK(h.log() == std::vector<std::string>{"ACK 2 seq=2",
                                            "TRD maker=1 taker=2 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("an IOC order respects its limit price", "[book][ioc]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 105, 50);
  h.clear();
  h.ioc(2, Side::Buy, 101, 50);  // not willing to pay 105
  CHECK(h.trades().empty());
  CHECK(h.log() == std::vector<std::string>{"ACK 2 seq=2",
                                            "CXL 2 unfilled=50 no_liquidity"});
}

// ───────────────────────────────────────────────────────────── fill or kill
//
// For when a partial fill is worse than no fill: buying half a hedge leaves you
// more exposed than buying none of it. Crucially, an unfillable FOK must not
// trade at all, not even the part it could have got.

TEMPLATE_TEST_CASE("a fully fillable FOK order trades", "[book][fok]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 200);
  h.clear();
  h.fok(2, Side::Buy, 101, 150);
  CHECK(h.log() == std::vector<std::string>{"ACK 2 seq=2",
                                            "TRD maker=1 taker=2 px=101 qty=150"});
}

TEMPLATE_TEST_CASE("an unfillable FOK order does not trade at all",
                   "[book][fok]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);  // only 50 available
  h.clear();
  h.fok(2, Side::Buy, 101, 130);    // wants 130
  CHECK(h.trades().empty());        // and takes none of the 50
  CHECK(h.log() == std::vector<std::string>{"ACK 2 seq=2",
                                            "CXL 2 unfilled=130 fok_unfillable"});
  CHECK(h.engine().book().best(Side::Sell) == 101);  // the 50 is untouched
}

TEMPLATE_TEST_CASE("a FOK order may fill across several price levels",
                   "[book][fok]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.limit(2, Side::Sell, 102, 100);
  h.clear();
  h.fok(3, Side::Buy, 102, 150);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=3 px=101 qty=50",
                                               "TRD maker=2 taker=3 px=102 qty=100"});
}

TEMPLATE_TEST_CASE("FOK counts only quantity within its limit price",
                   "[book][fok]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.limit(2, Side::Sell, 109, 500);  // plenty, but too expensive
  h.clear();
  h.fok(3, Side::Buy, 101, 130);
  CHECK(h.trades().empty());
  CHECK(h.log().back() == "CXL 3 unfilled=130 fok_unfillable");
}

TEMPLATE_TEST_CASE("a FOK order never rests", "[book][fok]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 200);
  h.clear();
  h.fok(2, Side::Buy, 101, 200);
  CHECK(h.engine().book().best(Side::Buy) == std::nullopt);
  CHECK(h.engine().book().best(Side::Sell) == std::nullopt);
}

TEMPLATE_TEST_CASE("a market FOK order needs the whole size available",
                   "[book][fok]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.limit(2, Side::Sell, 999, 40);  // any price is acceptable to a market order
  h.clear();
  h.market_fok(3, Side::Buy, 90);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=3 px=101 qty=50",
                                               "TRD maker=2 taker=3 px=999 qty=40"});
}

// ───────────────────────────────────────────────────────────────  post only
//
// A promise to be the maker and never the taker. Exchanges usually charge
// takers and pay makers a rebate, so this is how a participant guarantees it
// will not accidentally pay the fee.

TEMPLATE_TEST_CASE("a post-only order that does not cross rests normally",
                   "[book][postonly]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 105, 50);
  h.clear();
  h.post_only(2, Side::Buy, 101, 50);
  CHECK(h.log() == std::vector<std::string>{"ACK 2 seq=2"});
  CHECK(h.engine().book().best(Side::Buy) == 101);
}

TEMPLATE_TEST_CASE("a post-only order that would trade is rejected",
                   "[book][postonly]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.clear();
  h.post_only(2, Side::Buy, 101, 50);
  CHECK(h.log() == std::vector<std::string>{"REJ 2 post_only_would_cross"});
  CHECK(h.trades().empty());
  CHECK(h.engine().book().best(Side::Sell) == 101);  // book untouched
  CHECK(h.engine().book().best(Side::Buy) == std::nullopt);
}

TEMPLATE_TEST_CASE("a rejected post-only order consumes no sequence number",
                   "[book][postonly]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);  // seq 1
  h.post_only(2, Side::Buy, 101, 50);  // rejected
  h.clear();
  h.limit(3, Side::Buy, 99, 10);
  CHECK(h.log() == std::vector<std::string>{"ACK 3 seq=2"});
}

TEMPLATE_TEST_CASE("a post-only market order is a contradiction and is rejected",
                   "[book][postonly]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.post_only_market(1, Side::Buy, 50);
  CHECK(h.log() == std::vector<std::string>{"REJ 1 post_only_market"});
}
