#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "pokex/sim/simulator.hpp"

using namespace pokex;
using namespace pokex::sim;

namespace {

Config small_config(std::uint64_t seed) {
  Config c;
  c.seed = seed;
  c.ticks = 4000;
  c.frame_every = 100;
  return c;
}

// Order-sensitive digest of everything the UI would render, so determinism can
// be checked without diffing whole files.
std::uint64_t digest(const Simulator& sim) {
  std::uint64_t h = 0xCBF29CE484222325ull;
  const auto feed = [&h](std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  };
  for (const Frame& f : sim.frames()) {
    feed(f.tick);
    feed(static_cast<std::uint64_t>(f.best_bid));
    feed(static_cast<std::uint64_t>(f.best_ask));
    feed(static_cast<std::uint64_t>(f.candle.close));
    feed(f.candle.volume);
    for (const auto& lvl : f.bids) { feed(static_cast<std::uint64_t>(lvl.price)); feed(lvl.quantity); }
    for (const auto& lvl : f.asks) { feed(static_cast<std::uint64_t>(lvl.price)); feed(lvl.quantity); }
    for (const auto& a : f.agents) {
      feed(static_cast<std::uint64_t>(a.pnl));
      feed(static_cast<std::uint64_t>(a.inventory));
    }
  }
  return h;
}

}  // namespace

TEST_CASE("the simulation actually produces a market", "[sim]") {
  Simulator sim(small_config(1));
  sim.run();
  CHECK(sim.stats().trades > 0);        // agents traded with each other
  CHECK(sim.stats().cancels > 0);       // the maker re-quoted
  CHECK(sim.frames().size() > 10);      // frames were recorded
}

// Every trade moves cash from one agent to another and inventory the other way.
// Nothing is created or destroyed, so both must sum to zero across all agents at
// all times. This is the accounting equivalent of "the book never crosses".
TEST_CASE("cash and inventory are conserved across all agents", "[sim]") {
  for (std::uint64_t seed : {1u, 2u, 99u}) {
    Simulator sim(small_config(seed));
    sim.run();
    std::int64_t cash = 0;
    std::int64_t inventory = 0;
    for (const Agent& a : sim.agents()) {
      cash += a.cash;
      inventory += a.inventory;
    }
    CHECK(cash == 0);
    CHECK(inventory == 0);
  }
}

// Self-trade prevention does not land in the engine until A2, so the agent
// population has to avoid wash trades by construction: every agent is either a
// maker or a taker, never both, and the passive ones never cross the spread.
// Without this, over a quarter of the tape was an agent trading with itself,
// which is fake volume and makes the leaderboard meaningless.
TEST_CASE("no agent ever trades with itself", "[sim]") {
  for (std::uint64_t seed : {7u, 21u, 500u}) {
    Simulator sim(small_config(seed));
    sim.run();
    REQUIRE(sim.stats().trades > 100);
    CHECK(sim.stats().self_trades == 0);
    // The engine now enforces this too, since every agent is a participant.
    // Zero here means the agent design was genuinely sufficient on its own and
    // the engine never had to step in, rather than the engine quietly covering
    // for a population that would otherwise wash-trade.
    CHECK(sim.stats().self_trade_prevented == 0);
  }
}

TEST_CASE("the same seed produces an identical session", "[sim][determinism]") {
  for (std::uint64_t seed : {3u, 12345u}) {
    Simulator a(small_config(seed));
    Simulator b(small_config(seed));
    a.run();
    b.run();
    REQUIRE(a.frames().size() == b.frames().size());
    CHECK(digest(a) == digest(b));
  }
}

TEST_CASE("different seeds produce different sessions", "[sim]") {
  // Guards against a determinism test that passes because nothing varies.
  Simulator a(small_config(4));
  Simulator b(small_config(5));
  a.run();
  b.run();
  CHECK(digest(a) != digest(b));
}

// Prices have to stay tethered to fundamental value. They will not track it
// exactly, and should not: discovering it is the market's job, and the whole
// point of informed flow is that the tether is enforced by someone profiting
// from slack in it. But a persistent double-digit dislocation means no agent is
// anchored to anything exogenous.
//
// This caught a real bug. The market maker derived its reservation price from
// the mid, the mid is derived from the book, and the maker's post-only quotes
// sit at the top of the book, so the maker was quoting around its own quotes.
// That loop has no anchor and random-walked 21% away from fair value.
TEST_CASE("traded prices stay tethered to fundamental value", "[sim]") {
  Config c;
  c.seed = 42;
  c.ticks = 60000;
  c.frame_every = 50;
  Simulator sim(c);
  sim.run();

  double worst = 0.0;
  for (const Frame& f : sim.frames()) {
    if (!f.candle.high || !f.fundamental) continue;
    const double hi = std::abs(static_cast<double>(f.candle.high) - f.fundamental);
    const double lo = std::abs(static_cast<double>(f.candle.low) - f.fundamental);
    worst = std::max(worst, std::max(hi, lo) / f.fundamental);
  }
  CAPTURE(worst);
  CHECK(worst < 0.06);
}
