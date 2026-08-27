#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "helpers.hpp"

using namespace pokex;
using namespace pokex::test;

namespace {

// What the event stream says should be true of an order. Deliberately derived
// only from emitted events, so it can be cross-checked against the book's own
// state. Two independent views of the same truth.
struct Accounting {
  Side side{};
  OrderType type{};
  Quantity original{};
  Quantity filled{};
  Quantity cancelled{};
  bool accepted{false};
};

class Fuzzer {
 public:
  explicit Fuzzer(std::uint64_t seed) : rng_(seed) {}

  void run(int messages, int full_check_every = 100) {
    for (int i = 0; i < messages; ++i) {
      step();
      check_book_never_crosses();
      check_priority_ordering();
      if (i % full_check_every == 0) check_accounting();
    }
    check_accounting();
  }

  std::uint64_t trades() const { return trade_count_; }
  std::uint64_t cancels_sent() const { return cancels_sent_; }
  std::uint64_t messages_sent() const { return messages_sent_; }

 private:
  void step() {
    ++messages_sent_;
    // A cancel-heavy mix, because real venues are roughly 90% cancels and that
    // is the code path most likely to corrupt the book.
    const bool do_cancel = !live_.empty() && pick(0, 99) < 55;
    if (do_cancel) {
      ++cancels_sent_;
      const OrderId victim = live_[static_cast<std::size_t>(pick(0, static_cast<int>(live_.size()) - 1))];
      submit(CancelOrder{victim});
      return;
    }
    const OrderId id = next_id_++;
    const Side side = pick(0, 1) ? Side::Buy : Side::Sell;
    const bool market = pick(0, 99) < 12;
    const Quantity qty = static_cast<Quantity>(pick(1, 200));
    const Price price = static_cast<Price>(pick(95, 105));
    submit(NewOrder{id, side, market ? OrderType::Market : OrderType::Limit,
                    market ? 0 : price, qty});
  }

  void submit(const Command& c) {
    std::vector<Event> out;
    engine_.submit(c, [&](const Event& e) { out.push_back(e); });
    apply(c, out);
  }

  void apply(const Command& c, const std::vector<Event>& out) {
    if (const auto* n = std::get_if<NewOrder>(&c)) {
      auto& acct = book_[n->id];
      acct.side = n->side;
      acct.type = n->type;
      acct.original = n->quantity;
    }
    for (const auto& e : out) {
      if (const auto* a = std::get_if<Accepted>(&e)) {
        book_[a->id].accepted = true;
        live_.push_back(a->id);
      } else if (const auto* t = std::get_if<Trade>(&e)) {
        ++trade_count_;
        REQUIRE(t->quantity > 0);
        REQUIRE(t->maker_id != t->taker_id);
        book_[t->maker_id].filled += t->quantity;
        book_[t->taker_id].filled += t->quantity;
      } else if (const auto* x = std::get_if<Canceled>(&e)) {
        book_[x->id].cancelled += x->unfilled_quantity;
        drop_live(x->id);
      }
    }
    // Anything fully filled is no longer resting.
    for (const auto& e : out)
      if (const auto* t = std::get_if<Trade>(&e)) {
        for (OrderId id : {t->maker_id, t->taker_id}) {
          const auto& a = book_[id];
          if (a.filled >= a.original) drop_live(id);
        }
      }
  }

  void drop_live(OrderId id) {
    for (auto it = live_.begin(); it != live_.end(); ++it)
      if (*it == id) { live_.erase(it); return; }
  }

  // A book that crosses means we failed to execute a trade both sides had
  // already agreed to. Not "suboptimal": broken.
  void check_book_never_crosses() const {
    const auto bid = engine_.book().best(Side::Buy);
    const auto ask = engine_.book().best(Side::Sell);
    if (bid && ask) REQUIRE(*bid < *ask);
  }

  // Price-time priority, checked from the outside: best price first, and within
  // a price level, strictly increasing arrival sequence.
  void check_priority_ordering() const {
    for (const Side side : {Side::Buy, Side::Sell}) {
      bool first = true;
      Price last_price = 0;
      Sequence last_seq = 0;
      engine_.book().for_each(side, [&](const Order& o) {
        if (!first) {
          const bool improving = (side == Side::Buy) ? (o.price <= last_price)
                                                    : (o.price >= last_price);
          REQUIRE(improving);
          if (o.price == last_price) REQUIRE(o.seq > last_seq);
        }
        REQUIRE(o.remaining > 0);
        REQUIRE(o.remaining <= o.quantity);
        first = false;
        last_price = o.price;
        last_seq = o.seq;
      });
    }
  }

  // The strong one: what the book holds must equal what the event stream says
  // is still outstanding, order by order.
  void check_accounting() const {
    std::unordered_map<OrderId, Quantity> resting;
    for (const Side side : {Side::Buy, Side::Sell})
      engine_.book().for_each(side, [&](const Order& o) { resting[o.id] = o.remaining; });

    std::uint64_t resting_total = 0;
    for (const auto& [id, acct] : book_) {
      if (!acct.accepted) {
        REQUIRE(resting.count(id) == 0);  // rejected orders never enter the book
        continue;
      }
      REQUIRE(acct.filled <= acct.original);  // no order fills more than its size
      const auto it = resting.find(id);
      const Quantity actual = (it == resting.end()) ? 0 : it->second;
      // filled + cancelled + resting == original, for every order, always.
      REQUIRE(acct.filled + acct.cancelled + actual == acct.original);
      resting_total += actual;
    }
    REQUIRE(resting_total == engine_.book().total_remaining(Side::Buy) +
                                 engine_.book().total_remaining(Side::Sell));
  }

  int pick(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng_);
  }

  std::mt19937_64 rng_;
  MatchingEngine<BookV0Map> engine_{};
  std::unordered_map<OrderId, Accounting> book_{};
  std::vector<OrderId> live_{};
  OrderId next_id_{1};
  std::uint64_t trade_count_{0};
  std::uint64_t cancels_sent_{0};
  std::uint64_t messages_sent_{0};
};

// Replays a fixed command script and returns the event transcript.
std::vector<std::string> replay(const std::vector<Command>& script) {
  MatchingEngine<BookV0Map> engine;
  std::vector<std::string> out;
  for (const auto& c : script)
    engine.submit(c, [&](const Event& e) { out.push_back(render(e)); });
  return out;
}

std::vector<Command> random_script(std::uint64_t seed, int n) {
  std::mt19937_64 rng(seed);
  auto pick = [&](int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
  };
  std::vector<Command> script;
  std::vector<OrderId> issued;
  OrderId next = 1;
  for (int i = 0; i < n; ++i) {
    if (!issued.empty() && pick(0, 99) < 45) {
      script.push_back(CancelOrder{issued[static_cast<std::size_t>(pick(0, static_cast<int>(issued.size()) - 1))]});
      continue;
    }
    const OrderId id = next++;
    issued.push_back(id);
    const bool market = pick(0, 99) < 12;
    script.push_back(NewOrder{id, pick(0, 1) ? Side::Buy : Side::Sell,
                              market ? OrderType::Market : OrderType::Limit,
                              market ? 0 : static_cast<Price>(pick(95, 105)),
                              static_cast<Quantity>(pick(1, 200))});
  }
  return script;
}

}  // namespace

TEST_CASE("invariants hold over random order flow", "[property]") {
  for (std::uint64_t seed : {1u, 2u, 3u, 12345u, 99991u}) {
    Fuzzer f(seed);
    f.run(4000);
    CHECK(f.trades() > 0);        // the flow actually exercised matching
    CHECK(f.cancels_sent() > 0);  // and the cancel path
  }
}

TEST_CASE("invariants hold over a long random run", "[fuzz]") {
  Fuzzer f(0xC0FFEE);
  f.run(1'000'000, 5000);
  CHECK(f.messages_sent() == 1'000'000);
  CHECK(f.trades() > 0);
}

TEST_CASE("replaying the same script twice produces identical output", "[determinism]") {
  for (std::uint64_t seed : {7u, 8u, 424242u}) {
    const auto script = random_script(seed, 3000);
    const auto first = replay(script);
    const auto second = replay(script);
    REQUIRE(first.size() > 0);
    CHECK(first == second);
  }
}

TEST_CASE("two engines fed the same script agree at every step", "[determinism]") {
  const auto script = random_script(31337, 2000);
  MatchingEngine<BookV0Map> a, b;
  for (const auto& c : script) {
    std::vector<std::string> ea, eb;
    a.submit(c, [&](const Event& e) { ea.push_back(render(e)); });
    b.submit(c, [&](const Event& e) { eb.push_back(render(e)); });
    REQUIRE(ea == eb);
    REQUIRE(a.sequence() == b.sequence());
  }
}
