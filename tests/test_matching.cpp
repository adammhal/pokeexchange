#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace pokex;
using namespace pokex::test;

TEMPLATE_TEST_CASE("a limit order that cannot trade rests in the book", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, 99, 100);
  CHECK(h.log() == std::vector<std::string>{"ACK 1 seq=1"});
  CHECK(h.engine().book().best(Side::Buy) == 99);
}

TEMPLATE_TEST_CASE("a crossing limit order trades", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.clear();
  h.limit(2, Side::Buy, 101, 50);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=2 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("a trade prints at the maker's price, not the taker's", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.clear();
  h.limit(2, Side::Buy, 105, 50);  // willing to pay 105, should pay 101
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=2 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("a fully filled order does not rest", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.limit(2, Side::Buy, 101, 50);
  CHECK(h.engine().book().best(Side::Sell) == std::nullopt);
  CHECK(h.engine().book().best(Side::Buy) == std::nullopt);
}

TEMPLATE_TEST_CASE("orders at the same price fill in arrival order", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 10);  // first in
  h.limit(2, Side::Sell, 101, 10);  // second in
  h.clear();
  h.limit(3, Side::Buy, 101, 20);
  CHECK(h.trades() == std::vector<std::string>{
                          "TRD maker=1 taker=3 px=101 qty=10",
                          "TRD maker=2 taker=3 px=101 qty=10"});
}

TEMPLATE_TEST_CASE("the better price fills first, regardless of arrival order", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 103, 10);  // arrived first, but worse for the buyer
  h.limit(2, Side::Sell, 101, 10);  // arrived second, but cheapest
  h.clear();
  h.limit(3, Side::Buy, 103, 20);
  CHECK(h.trades() == std::vector<std::string>{
                          "TRD maker=2 taker=3 px=101 qty=10",
                          "TRD maker=1 taker=3 px=103 qty=10"});
}

TEMPLATE_TEST_CASE("a partial fill leaves the resting order with its remainder", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 150);
  h.clear();
  h.limit(2, Side::Buy, 101, 130);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=2 px=101 qty=130"});
  CHECK(h.engine().book().best(Side::Sell) == 101);
}

TEMPLATE_TEST_CASE("a partially filled resting order keeps its place in the queue", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.limit(2, Side::Sell, 101, 100);
  h.limit(3, Side::Buy, 101, 40);  // chips 40 off order 1
  h.clear();
  h.limit(4, Side::Buy, 101, 60);  // must still hit order 1's remaining 60
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=4 px=101 qty=60"});
}

TEMPLATE_TEST_CASE("a market order sweeps levels and cancels what it cannot fill", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.limit(2, Side::Sell, 102, 150);
  h.clear();
  h.market(3, Side::Buy, 400);
  CHECK(h.trades() == std::vector<std::string>{
                          "TRD maker=1 taker=3 px=101 qty=50",
                          "TRD maker=2 taker=3 px=102 qty=150"});
  auto l = h.log();
  REQUIRE(!l.empty());
  CHECK(l.back() == "CXL 3 unfilled=200");
}

TEMPLATE_TEST_CASE("a market order into an empty book is cancelled entirely", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.market(1, Side::Buy, 100);
  CHECK(h.log() == std::vector<std::string>{"ACK 1 seq=1", "CXL 1 unfilled=100"});
}

TEMPLATE_TEST_CASE("a limit order does not trade through its limit price", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 105, 50);
  h.clear();
  h.limit(2, Side::Buy, 101, 50);  // not willing to pay 105
  CHECK(h.trades().empty());
  CHECK(h.engine().book().best(Side::Buy) == 101);
}

TEMPLATE_TEST_CASE("cancel removes a resting order and reports its unfilled quantity", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, 99, 100);
  h.clear();
  h.cancel(1);
  CHECK(h.log() == std::vector<std::string>{"CXL 1 unfilled=100"});
  CHECK(h.engine().book().best(Side::Buy) == std::nullopt);
}

TEMPLATE_TEST_CASE("cancelling a partially filled order reports only what is left", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.limit(2, Side::Buy, 101, 30);
  h.clear();
  h.cancel(1);
  CHECK(h.log() == std::vector<std::string>{"CXL 1 unfilled=70"});
}

TEMPLATE_TEST_CASE("cancelling an unknown order is rejected", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.cancel(42);
  CHECK(h.log() == std::vector<std::string>{"REJ 42 unknown_order"});
}

TEMPLATE_TEST_CASE("a duplicate order id is rejected and leaves the book untouched", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, 99, 100);
  h.clear();
  h.limit(1, Side::Buy, 98, 5);
  CHECK(h.log() == std::vector<std::string>{"REJ 1 duplicate_order_id"});
  CHECK(h.engine().book().best(Side::Buy) == 99);
}

TEMPLATE_TEST_CASE("a zero quantity order is rejected", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, 99, 0);
  CHECK(h.log() == std::vector<std::string>{"REJ 1 zero_quantity"});
}

TEMPLATE_TEST_CASE("a rejected order does not consume a sequence number", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, 99, 0);  // rejected
  h.clear();
  h.limit(2, Side::Buy, 99, 10);
  CHECK(h.log() == std::vector<std::string>{"ACK 2 seq=1"});
}

TEMPLATE_TEST_CASE("a limit price below the tick domain is rejected", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, 0, 10);
  CHECK(h.log() == std::vector<std::string>{"REJ 1 price_out_of_range"});
}

TEMPLATE_TEST_CASE("a limit price above the tick domain is rejected", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 70000, 10);
  CHECK(h.log() == std::vector<std::string>{"REJ 1 price_out_of_range"});
}

TEMPLATE_TEST_CASE("a negative limit price is rejected", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, -5, 10);
  CHECK(h.log() == std::vector<std::string>{"REJ 1 price_out_of_range"});
}

TEMPLATE_TEST_CASE("a market order is not price-range checked", "[book]", POKEX_ALL_BOOKS) {
  // Market orders carry a meaningless price field (0, which is out of range).
  // Range checking them would reject every market order ever sent.
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.clear();
  h.market(2, Side::Buy, 50);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=2 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("orders at the domain boundaries are accepted", "[book]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, pokex::kMinPrice, 10);
  h.limit(2, Side::Sell, pokex::kMaxPrice, 10);
  CHECK(h.engine().book().best(Side::Buy) == pokex::kMinPrice);
  CHECK(h.engine().book().best(Side::Sell) == pokex::kMaxPrice);
}
