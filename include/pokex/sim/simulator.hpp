#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "pokex/book_v3_hash.hpp"
#include "pokex/events.hpp"
#include "pokex/matching.hpp"
#include "pokex/types.hpp"

// A population of trading agents driving the engine, so there is something to
// look at and something to measure.
//
// The engine stays clockless and deterministic; the simulator owns a virtual
// clock and a single seeded random stream. Same seed, same session, byte for
// byte, which is what makes the recorded UI demo reproducible.
namespace pokex::sim {

struct Config {
  std::uint64_t seed = 1;
  std::uint64_t ticks = 60000;
  std::uint64_t frame_every = 50;
  std::size_t depth = 15;  // price levels captured per side per frame

  // Every real desk has a position limit. Without one, agents accumulate
  // enormous inventories and their P&L becomes a bet on price direction rather
  // than a measure of how they trade.
  std::int64_t position_limit = 600;

  // Charizard: base stat total 534, rare. Fundamental value in ticks, where one
  // tick is a cent, so 4300 ticks reads as $43.00.
  double fundamental_start = 4300.0;
  double fundamental_vol = 0.00035;  // per tick, lognormal

  // Market maker: quotes both sides, skews against its own inventory.
  //
  // It prices off its own lagged, noisy estimate of fair value, NOT off the
  // book's mid. That distinction is load-bearing. Its post-only quotes sit at
  // the top of the book, so the mid *is* its quotes; deriving its reservation
  // price from the mid made it quote around its own quotes, a closed loop with
  // no exogenous anchor that random-walked 21% away from fair value.
  //
  // The lag is not a workaround, it is the point: the maker learns fair value
  // slowly while informed flow sees it at once, and that gap is exactly the
  // adverse selection a real maker is paid to bear.
  double mm_gamma = 0.55;        // how hard inventory pushes the quotes
  double mm_half_spread = 1.0;   // in ticks; tight enough to reach the top
  double mm_vol_k = 900.0;       // how much realised vol widens the spread
  double mm_value_alpha = 0.04;  // how fast its view of fair value catches up
  double mm_value_noise = 0.0015;
  Quantity mm_size = 60;

  // Liquidity provider: strictly passive, never crosses the spread. It ages its
  // own quotes out, without which the book would grow without bound and the
  // provider would end up holding an absurd position.
  double provider_rate = 0.55;
  int provider_offset_max = 14;
  std::size_t provider_max_resting = 300;
  double provider_value_noise = 0.0025;  // its view of fair value is imperfect

  // Informed flow: strictly aggressive, and it can see fundamental value, so it
  // systematically picks off quotes that have gone stale. This is the agent that
  // makes adverse selection visible on the leaderboard.
  double informed_rate = 0.18;
  double informed_bias = 3.0;

  std::uint64_t momentum_every = 25;
  double momentum_threshold = 0.8;  // in ticks of EMA separation
  std::uint64_t reversion_every = 30;
  double reversion_threshold = 4.0;

  double ema_fast_alpha = 0.06;
  double ema_slow_alpha = 0.008;
  double vol_alpha = 0.02;
};

struct BookLevel {
  Price price{};
  std::uint64_t quantity{};
};

struct Candle {
  Price open{}, high{}, low{}, close{};
  std::uint64_t volume{};
};

struct AgentSnapshot {
  std::int64_t pnl{};
  std::int64_t inventory{};
};

struct TapePrint {
  Price price{};
  std::uint64_t quantity{};
  Side aggressor{};
};

struct Frame {
  std::uint64_t tick{};
  Price best_bid{}, best_ask{};
  Price fundamental{};
  Candle candle{};
  std::vector<BookLevel> bids, asks;
  std::vector<AgentSnapshot> agents;
  std::vector<TapePrint> tape;
};

struct Agent {
  std::string name;
  std::int64_t cash{};       // signed, in tick-units of currency
  std::int64_t inventory{};  // signed, in units held
  std::uint64_t fills{};
};

struct Stats {
  std::uint64_t trades{};
  std::uint64_t cancel_commands{};  // cancels actually sent
  std::uint64_t cancels{};          // Canceled events, which also fire when a
                                    // market order's remainder is dropped
  std::uint64_t self_trades{};
  std::uint64_t orders{};
  std::uint64_t rejects{};
};

class Simulator {
 public:
  // Agent slots. Fixed order, because the UI leaderboard indexes into it.
  //
  // Every agent is either a maker or a taker, never both. That is what makes
  // "no agent trades with itself" a real invariant rather than a hope: self
  // trade prevention does not land in the engine until A2, so the agent
  // population has to avoid generating wash trades on its own.
  enum : int {
    kMaker = 0,
    kMomentum = 1,
    kReversion = 2,
    kProvider = 3,
    kInformed = 4,
    kAgentCount = 5
  };

  explicit Simulator(const Config& cfg)
      : cfg_(cfg), rng_(cfg.seed), fundamental_(cfg.fundamental_start) {
    agents_.push_back({"Market Maker", 0, 0, 0});
    agents_.push_back({"Momentum", 0, 0, 0});
    agents_.push_back({"Mean Reversion", 0, 0, 0});
    agents_.push_back({"Liquidity Provider", 0, 0, 0});
    agents_.push_back({"Informed Flow", 0, 0, 0});
    mid_ = ema_fast_ = ema_slow_ = mm_value_ = fundamental_;
    last_trade_ = static_cast<Price>(std::llround(fundamental_));
    open_candle(last_trade_);
  }

  void run() {
    for (std::uint64_t t = 0; t < cfg_.ticks; ++t) {
      tick_ = t;
      step_fundamental();
      act_market_maker();
      act_momentum();
      act_reversion();
      act_provider();
      act_informed();
      update_market_state();
      if ((t + 1) % cfg_.frame_every == 0) record_frame();
    }
  }

  const std::vector<Frame>& frames() const { return frames_; }
  const std::vector<Agent>& agents() const { return agents_; }
  const Stats& stats() const { return stats_; }
  const Config& config() const { return cfg_; }
  double mid() const { return mid_; }

 private:
  struct Owner {
    int agent{};
    Side side{};
    Quantity remaining{};
  };

  // ------------------------------------------------------------- order entry
  OrderId submit(const Command& cmd) {
    engine_.submit(cmd, [this](const Event& e) { handle(e); });
    return 0;
  }

  // Would this order breach the agent's position limit if it filled entirely?
  bool within_limit(int agent, Side side, Quantity qty) const {
    const std::int64_t inventory = agents_[static_cast<std::size_t>(agent)].inventory;
    const auto size = static_cast<std::int64_t>(qty);
    return (side == Side::Buy) ? (inventory + size <= cfg_.position_limit)
                               : (inventory - size >= -cfg_.position_limit);
  }

  OrderId send_limit(int agent, Side side, Price price, Quantity qty) {
    if (qty == 0 || !within_limit(agent, side, qty)) return 0;
    const OrderId id = next_id_++;
    owner_.emplace(id, Owner{agent, side, qty});
    ++stats_.orders;
    engine_.submit(NewOrder{id, side, OrderType::Limit, clamp(price), qty},
                   [this](const Event& e) { handle(e); });
    return id;
  }

  void send_market(int agent, Side side, Quantity qty) {
    if (qty == 0 || !within_limit(agent, side, qty)) return;
    const OrderId id = next_id_++;
    owner_.emplace(id, Owner{agent, side, qty});
    ++stats_.orders;
    engine_.submit(NewOrder{id, side, OrderType::Market, 0, qty},
                   [this](const Event& e) { handle(e); });
  }

  void send_cancel(int agent, OrderId id) {
    (void)agent;
    ++stats_.cancel_commands;
    engine_.submit(CancelOrder{id}, [this](const Event& e) { handle(e); });
  }

  // ------------------------------------------------------------ event intake
  void handle(const Event& event) {
    if (const auto* t = std::get_if<Trade>(&event)) {
      settle(*t);
      return;
    }
    if (const auto* c = std::get_if<Canceled>(&event)) {
      ++stats_.cancels;
      owner_.erase(c->id);
      return;
    }
    if (std::holds_alternative<Rejected>(event)) ++stats_.rejects;
  }

  // Every trade is a transfer: one agent's cash becomes another's, and
  // inventory moves the other way. Both therefore sum to zero across all
  // agents, forever, which is what the conservation test checks.
  void settle(const Trade& trade) {
    ++stats_.trades;
    const auto maker = owner_.find(trade.maker_id);
    const auto taker = owner_.find(trade.taker_id);
    if (maker == owner_.end() || taker == owner_.end()) return;  // not ours

    const int maker_agent = maker->second.agent;
    const int taker_agent = taker->second.agent;
    if (maker_agent == taker_agent) ++stats_.self_trades;

    const Side taker_side = taker->second.side;
    const auto notional =
        static_cast<std::int64_t>(trade.price) * static_cast<std::int64_t>(trade.quantity);
    const auto qty = static_cast<std::int64_t>(trade.quantity);

    Agent& buyer = agents_[static_cast<std::size_t>(
        taker_side == Side::Buy ? taker_agent : maker_agent)];
    Agent& seller = agents_[static_cast<std::size_t>(
        taker_side == Side::Buy ? maker_agent : taker_agent)];
    buyer.cash -= notional;
    buyer.inventory += qty;
    seller.cash += notional;
    seller.inventory -= qty;
    ++buyer.fills;
    ++seller.fills;

    last_trade_ = trade.price;
    update_candle(trade.price, trade.quantity);
    if (tape_.size() < 64) tape_.push_back(TapePrint{trade.price, trade.quantity, taker_side});

    decrement(maker, trade.quantity);
    decrement(taker, trade.quantity);
  }

  void decrement(std::unordered_map<OrderId, Owner>::iterator it, Quantity filled) {
    if (it->second.remaining <= filled) {
      owner_.erase(it);
      return;
    }
    it->second.remaining -= filled;
  }

  // ---------------------------------------------------------------- agents
  // Quotes both sides, then shifts both away from whatever it is holding. A
  // long position produces cheaper offers and stingier bids, so the position
  // works itself off. This is the simple cousin of Avellaneda-Stoikov; the full
  // model comes later.
  void act_market_maker() {
    if (mm_bid_ && owner_.count(mm_bid_)) send_cancel(kMaker, mm_bid_);
    if (mm_ask_ && owner_.count(mm_ask_)) send_cancel(kMaker, mm_ask_);
    mm_bid_ = mm_ask_ = 0;

    // Track fair value with lag and noise, from the fundamental rather than
    // from the book. See the note on mm_value_alpha above.
    mm_value_ += cfg_.mm_value_alpha *
                 (fundamental_ * (1.0 + cfg_.mm_value_noise * normal()) - mm_value_);

    const Agent& a = agents_[kMaker];
    const double reservation = mm_value_ - static_cast<double>(a.inventory) * cfg_.mm_gamma;
    const double half = cfg_.mm_half_spread +
                        cfg_.mm_vol_k * std::sqrt(std::max(vol_, 0.0)) * mm_value_ * 0.001;

    Price bid = clamp(static_cast<Price>(std::llround(reservation - half)));
    Price ask = clamp(static_cast<Price>(std::llround(reservation + half)));

    // Stay passive. A maker that crosses the spread is paying for immediacy it
    // is supposed to be selling, and its whole edge is being the one who waits.
    const auto best_bid = engine_.book().best(Side::Buy);
    const auto best_ask = engine_.book().best(Side::Sell);
    if (best_ask) bid = std::min(bid, static_cast<Price>(*best_ask - 1));
    if (best_bid) ask = std::max(ask, static_cast<Price>(*best_bid + 1));
    bid = clamp(bid);
    ask = clamp(ask);
    if (ask <= bid) ask = clamp(bid + 1);

    mm_bid_ = send_limit(kMaker, Side::Buy, bid, cfg_.mm_size);
    mm_ask_ = send_limit(kMaker, Side::Sell, ask, cfg_.mm_size);
  }

  void act_momentum() {
    if (tick_ % cfg_.momentum_every != 0) return;
    const double signal = ema_fast_ - ema_slow_;
    if (std::abs(signal) < cfg_.momentum_threshold) return;
    send_market(kMomentum, signal > 0 ? Side::Buy : Side::Sell,
                static_cast<Quantity>(20 + uniform_int(0, 40)));
  }

  void act_reversion() {
    if (tick_ % cfg_.reversion_every != 0) return;
    const double deviation = mid_ - ema_slow_;
    if (std::abs(deviation) < cfg_.reversion_threshold) return;
    send_market(kReversion, deviation > 0 ? Side::Sell : Side::Buy,
                static_cast<Quantity>(20 + uniform_int(0, 40)));
  }

  // Builds the depth behind the maker's two quotes. Strictly passive: it posts
  // at or behind the current best on its own side, so it can never trade on
  // arrival and can never cross one of its own earlier orders, because the book
  // guarantees best_bid < best_ask.
  void act_provider() {
    if (uniform() >= cfg_.provider_rate) return;
    // Lean against its own inventory, so a long position gets worked off rather
    // than compounded.
    const double lean =
        static_cast<double>(agents_[kProvider].inventory) / static_cast<double>(cfg_.position_limit);
    const double p_buy = std::clamp(0.5 - 0.45 * lean, 0.05, 0.95);
    const Side side = (uniform() < p_buy) ? Side::Buy : Side::Sell;
    const auto qty = static_cast<Quantity>(5 + uniform_int(0, 55));
    const int offset = 1 + uniform_int(0, cfg_.provider_offset_max);

    // Quote around its own noisy estimate of fair value, not around the current
    // best. Anchoring to the best is self-referential: the mid is derived from
    // the book, so a book quoted off the mid has nothing holding it anywhere and
    // free-drifts. A dealer quotes around what it thinks the thing is worth.
    const double value = fundamental_ * (1.0 + cfg_.provider_value_noise * normal());
    double desired = (side == Side::Buy) ? value - offset : value + offset;

    // Post-only. Clamping to not cross also guarantees it can never hit one of
    // its own resting orders, whichever side of the book they are on.
    const auto best_bid = engine_.book().best(Side::Buy);
    const auto best_ask = engine_.book().best(Side::Sell);
    if (side == Side::Buy && best_ask) desired = std::min(desired, static_cast<double>(*best_ask - 1));
    if (side == Side::Sell && best_bid) desired = std::max(desired, static_cast<double>(*best_bid + 1));

    const OrderId id =
        send_limit(kProvider, side, static_cast<Price>(std::llround(desired)), qty);
    if (id) resting_.push_back(id);

    // Age out the oldest quotes. Real providers re-quote constantly rather than
    // leaving orders to rot, and this is also what makes the message mix
    // realistically cancel-heavy.
    while (resting_.size() > cfg_.provider_max_resting) {
      const OrderId stale = resting_.front();
      resting_.pop_front();
      if (owner_.count(stale)) send_cancel(kProvider, stale);
    }
  }

  // Sees fundamental value and trades toward it, which means it buys when the
  // book is too cheap and sells when it is too rich. From the quoting agents'
  // point of view that is adverse selection: their fills arrive precisely when
  // they are about to be wrong. Strictly aggressive, so it never rests an order.
  void act_informed() {
    if (uniform() >= cfg_.informed_rate) return;
    const double gap = (fundamental_ - mid_) / std::max(1.0, fundamental_);
    const double p_buy = std::clamp(0.5 + gap * cfg_.informed_bias, 0.05, 0.95);
    const Side side = (uniform() < p_buy) ? Side::Buy : Side::Sell;
    send_market(kInformed, side, static_cast<Quantity>(5 + uniform_int(0, 45)));
  }

  // ---------------------------------------------------------- market state
  void step_fundamental() {
    fundamental_ *= std::exp(cfg_.fundamental_vol * normal());
    fundamental_ = std::clamp(fundamental_, 200.0, 60000.0);
  }

  void update_market_state() {
    const auto bid = engine_.book().best(Side::Buy);
    const auto ask = engine_.book().best(Side::Sell);
    const double previous = mid_;
    if (bid && ask) mid_ = (static_cast<double>(*bid) + static_cast<double>(*ask)) / 2.0;
    else if (bid) mid_ = static_cast<double>(*bid);
    else if (ask) mid_ = static_cast<double>(*ask);
    else mid_ = static_cast<double>(last_trade_);

    ema_fast_ += cfg_.ema_fast_alpha * (mid_ - ema_fast_);
    ema_slow_ += cfg_.ema_slow_alpha * (mid_ - ema_slow_);
    const double ret = (previous > 0) ? (mid_ - previous) / previous : 0.0;
    vol_ = (1.0 - cfg_.vol_alpha) * vol_ + cfg_.vol_alpha * ret * ret;
  }

  // ------------------------------------------------------------- recording
  void open_candle(Price at) { candle_ = Candle{at, at, at, at, 0}; }

  void update_candle(Price price, Quantity qty) {
    if (candle_.volume == 0) {
      candle_ = Candle{price, price, price, price, qty};
      return;
    }
    candle_.high = std::max(candle_.high, price);
    candle_.low = std::min(candle_.low, price);
    candle_.close = price;
    candle_.volume += qty;
  }

  std::vector<BookLevel> top_levels(Side side) const {
    std::vector<BookLevel> out;
    engine_.book().for_each(side, [&](const Order& order) {
      if (!out.empty() && out.back().price == order.price) {
        out.back().quantity += order.remaining;
        return;
      }
      if (out.size() >= cfg_.depth) return;
      out.push_back(BookLevel{order.price, order.remaining});
    });
    return out;
  }

  void record_frame() {
    Frame f;
    f.tick = tick_ + 1;
    f.best_bid = engine_.book().best(Side::Buy).value_or(0);
    f.best_ask = engine_.book().best(Side::Sell).value_or(0);
    f.fundamental = static_cast<Price>(std::llround(fundamental_));
    f.bids = top_levels(Side::Buy);
    f.asks = top_levels(Side::Sell);
    if (candle_.volume == 0) open_candle(last_trade_);
    f.candle = candle_;
    for (const Agent& a : agents_)
      f.agents.push_back(AgentSnapshot{
          a.cash + static_cast<std::int64_t>(static_cast<double>(a.inventory) * mid_),
          a.inventory});
    f.tape = tape_;
    frames_.push_back(std::move(f));
    tape_.clear();
    open_candle(last_trade_);
  }

  // ---------------------------------------------------------------- helpers
  static Price clamp(Price p) { return std::clamp(p, kMinPrice, kMaxPrice); }
  double uniform() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_); }
  int uniform_int(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng_);
  }
  double normal() { return std::normal_distribution<double>(0.0, 1.0)(rng_); }

  Config cfg_;
  std::mt19937_64 rng_;
  MatchingEngine<BookV3Hash> engine_{};

  std::vector<Agent> agents_{};
  std::vector<Frame> frames_{};
  std::vector<TapePrint> tape_{};
  Stats stats_{};

  std::unordered_map<OrderId, Owner> owner_{};
  OrderId next_id_{1};
  std::uint64_t tick_{0};

  double fundamental_{};
  double mm_value_{};  // the maker's lagged, noisy view of fair value
  double mid_{};
  double ema_fast_{};
  double ema_slow_{};
  double vol_{0.0};
  Price last_trade_{};
  Candle candle_{};

  std::deque<OrderId> resting_{};  // provider's quotes, oldest first
  OrderId mm_bid_{0};
  OrderId mm_ask_{0};
};

}  // namespace pokex::sim
