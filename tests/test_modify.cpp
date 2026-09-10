#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace pokex;
using namespace pokex::test;

// Time priority is compensation for exposure risk, so an order keeps its place
// in the queue exactly as long as it has not increased the risk anyone else is
// taking on its behalf. Reducing quantity lowers your own exposure and harms
// nobody behind you, so there is nothing to charge for. Anything else is
// queue-jumping: the extra size never waited, and the new price level was never
// waited at at all.
//
// Queue position is not directly observable, so these tests establish it the
// only way that matters: by seeing who gets filled first.

// ───────────────────────────────────────────────────────── keeping priority

TEMPLATE_TEST_CASE("reducing quantity keeps queue position", "[book][modify]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);  // first in the queue
  h.limit(2, Side::Sell, 101, 100);  // second
  h.modify(1, 101, 60);              // same price, smaller: keeps its place
  h.clear();
  h.limit(3, Side::Buy, 101, 60);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=3 px=101 qty=60"});
}

TEMPLATE_TEST_CASE("reducing quantity reports that priority was kept",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.clear();
  h.modify(1, 101, 60);
  CHECK(h.log() == std::vector<std::string>{"MOD 1 qty=60 px=101 seq=1 kept"});
}

TEMPLATE_TEST_CASE("a kept-priority modify does not change the order's sequence",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);  // gets seq 1
  h.limit(2, Side::Sell, 101, 100);  // gets seq 2
  h.clear();
  h.modify(1, 101, 50);
  // Still seq 1. If it had been re-sequenced it would be behind order 2.
  CHECK(h.log() == std::vector<std::string>{"MOD 1 qty=50 px=101 seq=1 kept"});
}

// ───────────────────────────────────────────────────────── losing priority

TEMPLATE_TEST_CASE("increasing quantity loses queue position", "[book][modify]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);  // first in the queue
  h.limit(2, Side::Sell, 101, 100);  // second
  h.modify(1, 101, 150);             // asks for more, so it goes to the back
  h.clear();
  h.limit(3, Side::Buy, 101, 100);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=2 taker=3 px=101 qty=100"});
}

TEMPLATE_TEST_CASE("increasing quantity reports that priority was lost",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.clear();
  h.modify(1, 101, 150);
  CHECK(h.log() == std::vector<std::string>{"MOD 1 qty=150 px=101 seq=2 lost"});
}

TEMPLATE_TEST_CASE("changing price loses queue position", "[book][modify]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 50);
  h.limit(2, Side::Sell, 102, 50);
  h.modify(1, 103, 50);  // moves away; 102 is now the best ask
  h.clear();
  h.limit(3, Side::Buy, 102, 50);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=2 taker=3 px=102 qty=50"});
}

TEMPLATE_TEST_CASE("a repriced order goes behind orders already at that price",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 100, 50);  // will move to 101
  h.limit(2, Side::Sell, 101, 50);  // already waiting at 101
  h.modify(1, 101, 50);
  h.clear();
  h.limit(3, Side::Buy, 101, 50);
  // Order 2 waited at 101; order 1 has only just arrived there.
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=2 taker=3 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("a reprice that crosses the book trades immediately",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Buy, 99, 50);    // resting bid
  h.limit(2, Side::Sell, 101, 50);  // resting ask
  h.clear();
  h.modify(1, 101, 50);             // bid moves up onto the ask
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=2 taker=1 px=101 qty=50"});
  CHECK(h.engine().book().best(Side::Sell) == std::nullopt);
  CHECK(h.engine().book().best(Side::Buy) == std::nullopt);
}

// ─────────────────────────────────────────── quantity is the new total

TEMPLATE_TEST_CASE("modify quantity counts what has already been filled",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.limit(2, Side::Buy, 101, 30);  // order 1 now has 30 filled, 70 remaining
  h.clear();
  h.modify(1, 101, 80);            // new total 80, so 50 should remain
  CHECK(h.log() == std::vector<std::string>{"MOD 1 qty=80 px=101 seq=1 kept"});
  h.clear();
  h.limit(3, Side::Buy, 101, 200);
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=3 px=101 qty=50"});
}

TEMPLATE_TEST_CASE("modifying down to the filled quantity cancels the order",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.limit(2, Side::Buy, 101, 30);  // 30 filled
  h.clear();
  h.modify(1, 101, 30);            // nothing left to work
  CHECK(h.log() == std::vector<std::string>{"CXL 1 unfilled=70 user"});
  CHECK(h.engine().book().best(Side::Sell) == std::nullopt);
}

// ──────────────────────────────────────────────────────────── rejections

TEMPLATE_TEST_CASE("modifying an unknown order is rejected", "[book][modify]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.modify(42, 101, 50);
  CHECK(h.log() == std::vector<std::string>{"REJ 42 unknown_order"});
}

TEMPLATE_TEST_CASE("modifying to zero quantity is rejected", "[book][modify]",
                   POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.clear();
  h.modify(1, 101, 0);
  CHECK(h.log() == std::vector<std::string>{"REJ 1 zero_quantity"});
  CHECK(h.engine().book().best(Side::Sell) == 101);  // untouched
}

TEMPLATE_TEST_CASE("modifying to a price outside the tick domain is rejected",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.clear();
  h.modify(1, 70000, 100);
  CHECK(h.log() == std::vector<std::string>{"REJ 1 price_out_of_range"});
  CHECK(h.engine().book().best(Side::Sell) == 101);
}

TEMPLATE_TEST_CASE("a rejected modify leaves the order exactly as it was",
                   "[book][modify]", POKEX_ALL_BOOKS) {
  Harness<TestType> h;
  h.limit(1, Side::Sell, 101, 100);
  h.limit(2, Side::Sell, 101, 100);
  h.modify(1, 101, 0);  // rejected
  h.clear();
  h.limit(3, Side::Buy, 101, 100);
  // Order 1 still has its original size and its original place in the queue.
  CHECK(h.trades() == std::vector<std::string>{"TRD maker=1 taker=3 px=101 qty=100"});
}
