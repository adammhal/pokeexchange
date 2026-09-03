// pokex-sim: runs the agent population against the engine and records a session
// the UI can replay.
//
// The output is a recording, not a live feed, which is deliberate: it makes the
// browser demo a static file that GitHub Pages can serve, and it makes the
// screenshots reproducible. A WebSocket source can be swapped in later behind
// the same schema.
//
//   pokex-sim [--seed S] [--ticks N] [--frame-every N] [--out session.json]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "pokex/sim/simulator.hpp"

using namespace pokex;
using namespace pokex::sim;

namespace {

// Keys are short on purpose. At a couple of thousand frames the difference
// between "quantity" and "q" is most of the file size, and this file has to be
// downloaded by a browser.
void write_session(std::ostream& out, const Simulator& sim) {
  const Config& cfg = sim.config();
  const Stats& stats = sim.stats();

  out << "{\n\"meta\":{"
      << "\"instrument\":\"Charizard\",\"symbol\":\"CHZ\",\"tick_size\":0.01"
      << ",\"seed\":" << cfg.seed << ",\"ticks\":" << cfg.ticks
      << ",\"frame_every\":" << cfg.frame_every
      << ",\"position_limit\":" << cfg.position_limit
      << ",\"orders\":" << stats.orders << ",\"trades\":" << stats.trades
      << ",\"cancels\":" << stats.cancel_commands
      << ",\"self_trades\":" << stats.self_trades
      << ",\"rejects\":" << stats.rejects << ",\"agents\":[";
  const auto& agents = sim.agents();
  for (std::size_t i = 0; i < agents.size(); ++i)
    out << "\"" << agents[i].name << "\"" << (i + 1 < agents.size() ? "," : "");
  out << "]},\n\"frames\":[\n";

  const auto& frames = sim.frames();
  for (std::size_t fi = 0; fi < frames.size(); ++fi) {
    const Frame& f = frames[fi];
    out << "{\"t\":" << f.tick << ",\"bb\":" << f.best_bid << ",\"ba\":" << f.best_ask
        << ",\"f\":" << f.fundamental << ",\"c\":[" << f.candle.open << ","
        << f.candle.high << "," << f.candle.low << "," << f.candle.close << ","
        << f.candle.volume << "]";

    out << ",\"b\":[";
    for (std::size_t i = 0; i < f.bids.size(); ++i)
      out << "[" << f.bids[i].price << "," << f.bids[i].quantity << "]"
          << (i + 1 < f.bids.size() ? "," : "");
    out << "],\"a\":[";
    for (std::size_t i = 0; i < f.asks.size(); ++i)
      out << "[" << f.asks[i].price << "," << f.asks[i].quantity << "]"
          << (i + 1 < f.asks.size() ? "," : "");
    out << "]";

    out << ",\"p\":[";
    for (std::size_t i = 0; i < f.agents.size(); ++i)
      out << f.agents[i].pnl << (i + 1 < f.agents.size() ? "," : "");
    out << "],\"i\":[";
    for (std::size_t i = 0; i < f.agents.size(); ++i)
      out << f.agents[i].inventory << (i + 1 < f.agents.size() ? "," : "");
    out << "]";

    out << ",\"tp\":[";
    for (std::size_t i = 0; i < f.tape.size(); ++i)
      out << "[" << f.tape[i].price << "," << f.tape[i].quantity << ","
          << (f.tape[i].aggressor == Side::Buy ? 1 : 0) << "]"
          << (i + 1 < f.tape.size() ? "," : "");
    out << "]}" << (fi + 1 < frames.size() ? "," : "") << "\n";
  }
  out << "]}\n";
}

}  // namespace

int main(int argc, char** argv) {
  Config cfg;
  std::string path = "session.json";

  for (int i = 1; i < argc; ++i) {
    const auto has_next = [&] { return i + 1 < argc; };
    if (!std::strcmp(argv[i], "--seed") && has_next())
      cfg.seed = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--ticks") && has_next())
      cfg.ticks = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--frame-every") && has_next())
      cfg.frame_every = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--out") && has_next())
      path = argv[++i];
    else {
      std::cerr << "usage: pokex-sim [--seed S] [--ticks N] [--frame-every N] "
                   "[--out session.json]\n";
      return 2;
    }
  }

  Simulator sim(cfg);
  sim.run();

  std::ofstream out(path);
  if (!out) {
    std::cerr << "pokex-sim: cannot write " << path << "\n";
    return 2;
  }
  write_session(out, sim);
  out.flush();

  const Stats& s = sim.stats();
  std::printf("%llu ticks, %zu frames -> %s\n", (unsigned long long)cfg.ticks,
              sim.frames().size(), path.c_str());
  const auto messages = s.orders + s.cancel_commands;
  std::printf("%llu messages: %llu new orders, %llu cancels (%.1f%%). "
              "%llu trades, %llu self-trades, %llu rejects\n",
              (unsigned long long)messages, (unsigned long long)s.orders,
              (unsigned long long)s.cancel_commands,
              100.0 * static_cast<double>(s.cancel_commands) /
                  static_cast<double>(messages ? messages : 1),
              (unsigned long long)s.trades, (unsigned long long)s.self_trades,
              (unsigned long long)s.rejects);
  std::printf("final mid %.2f\n\n", sim.mid() / 100.0);
  std::printf("%-20s %14s %10s %8s\n", "agent", "P&L", "inventory", "fills");
  for (const Agent& a : sim.agents()) {
    const auto pnl = a.cash + static_cast<std::int64_t>(
                                  static_cast<double>(a.inventory) * sim.mid());
    std::printf("%-20s %13.2f %10lld %8llu\n", a.name.c_str(),
                static_cast<double>(pnl) / 100.0, (long long)a.inventory,
                (unsigned long long)a.fills);
  }
  return 0;
}
