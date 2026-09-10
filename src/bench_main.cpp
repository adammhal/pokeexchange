// pokex-bench: replays one fixed order flow through every book version and
// reports throughput and latency percentiles.
//
// The comparison is only meaningful if the versions did the same work, so this
// tool verifies that all four produce byte-identical event streams before it
// reports a single timing number. If they diverge, it refuses to print a result.
//
//   pokex-bench [--messages N] [--seed S] [--cancel-pct P] [--json out.json]
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "bench_stats.hpp"
#include "flow.hpp"
#include "pokex/all_books.hpp"
#include "pokex/matching.hpp"
#include "pokex/text_codec.hpp"

using namespace pokex;
using Clock = std::chrono::steady_clock;

namespace {

using pokex::bench::Flow;
using pokex::bench::FlowOptions;
using pokex::bench::Mix;
using pokex::bench::Scenario;
using pokex::bench::build_flow;
using pokex::bench::find_scenario;
using pokex::bench::kScenarios;

struct Options : FlowOptions {
  std::string only{};  // run just one scenario by name
  std::string json{};
};

// Cheap order-sensitive hash of the whole event stream, so equivalence can be
// checked without keeping four multi-million-line transcripts in memory.
struct StreamHash {
  std::uint64_t h = 0xCBF29CE484222325ull;
  void feed(std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  }
  void operator()(const Event& e) {
    std::visit(
        [this](const auto& v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, Accepted>) {
            feed(1); feed(v.id); feed(v.seq);
          } else if constexpr (std::is_same_v<T, Rejected>) {
            feed(2); feed(v.id); feed(static_cast<std::uint64_t>(v.reason));
          } else if constexpr (std::is_same_v<T, Trade>) {
            feed(3); feed(v.maker_id); feed(v.taker_id);
            feed(static_cast<std::uint64_t>(v.price)); feed(v.quantity); feed(v.seq);
          } else if constexpr (std::is_same_v<T, Modified>) {
            feed(5); feed(v.id); feed(v.quantity);
            feed(static_cast<std::uint64_t>(v.price)); feed(v.seq);
            feed(v.kept_priority ? 1u : 0u);
          } else {
            feed(4); feed(v.id); feed(v.unfilled_quantity);
          }
        },
        e);
  }
};

struct Result {
  std::string name;
  std::uint64_t stream_hash = 0;
  std::size_t events = 0;
  double throughput = 0;  // messages per second, from bulk timing
  double mean_ns = 0;     // exact: derived from bulk timing, not from the samples
  bench::Samples latency;
};

template <typename BookT>
void apply(MatchingEngine<BookT>& engine, const std::vector<Command>& cmds) {
  std::uint64_t sink = 0;
  for (const auto& c : cmds) engine.submit(c, [&](const Event&) { ++sink; });
  asm volatile("" : : "r"(sink) : "memory");
}

template <typename BookT>
Result measure(const char* name, const Flow& flow) {
  Result r;
  r.name = name;

  // Pass 1, untimed: verify what this version produces, over the whole flow.
  {
    MatchingEngine<BookT> engine;
    StreamHash hash;
    std::size_t events = 0;
    const auto sink = [&](const Event& e) { hash(e); ++events; };
    for (const auto& c : flow.preload) engine.submit(c, sink);
    for (const auto& c : flow.steady) engine.submit(c, sink);
    r.stream_hash = hash.h;
    r.events = events;
  }

  // Pass 2: bulk timing of the steady flow only. The book is preloaded first,
  // untimed, which both warms the caches and means we measure steady-state
  // behaviour against a realistically large book rather than an empty one.
  {
    MatchingEngine<BookT> engine;
    apply(engine, flow.preload);
    std::uint64_t sink = 0;
    const auto start = Clock::now();
    for (const auto& c : flow.steady) engine.submit(c, [&](const Event&) { ++sink; });
    const auto elapsed = Clock::now() - start;
    asm volatile("" : : "r"(sink) : "memory");
    const double secs =
        std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
    r.throughput = static_cast<double>(flow.steady.size()) / secs;
    r.mean_ns = secs * 1e9 / static_cast<double>(flow.steady.size());
  }

  // Pass 3: per-message timing, for the tail. On a clock with ~42ns granularity
  // the low percentiles are quantised to that grid and say little; the high
  // percentiles and the max resolve real multi-microsecond outliers, which is
  // where allocation stalls and rehashes show up.
  {
    MatchingEngine<BookT> engine;
    apply(engine, flow.preload);
    r.latency.reserve(flow.steady.size());
    std::uint64_t sink = 0;
    for (const auto& c : flow.steady) {
      const auto a = Clock::now();
      engine.submit(c, [&](const Event&) { ++sink; });
      const auto b = Clock::now();
      r.latency.add(static_cast<std::uint32_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()));
    }
    asm volatile("" : : "r"(sink) : "memory");
    r.latency.finalize();
  }
  return r;
}

struct ScenarioRun {
  const Scenario* scenario;
  Mix mix;
  std::vector<Result> results;
};

void write_json(const std::string& path, const Options& opt, bench::ClockFacts clock,
                const std::vector<ScenarioRun>& runs) {
  std::ofstream out(path);
  if (!out) {
    std::cerr << "pokex-bench: cannot write " << path << "\n";
    return;
  }
  const bool as_js = path.size() > 3 && path.compare(path.size() - 3, 3, ".js") == 0;
  if (as_js) out << "window.POKEX_BENCH=";
  out << "{\n  \"messages_per_scenario\": " << opt.messages
      << ",\n  \"seed\": " << opt.seed
      << ",\n  \"clock_overhead_ns\": " << clock.overhead_ns
      << ",\n  \"clock_granularity_ns\": " << clock.granularity_ns
      << ",\n  \"scenarios\": [\n";
  for (std::size_t si = 0; si < runs.size(); ++si) {
    const auto& run = runs[si];
    const double base = run.results.front().throughput;
    out << "    {\n      \"name\": \"" << run.scenario->name << "\",\n"
        << "      \"description\": \"" << run.scenario->description << "\",\n"
        << "      \"resident_orders\": " << run.scenario->resident << ",\n"
        << "      \"price_levels\": " << run.scenario->levels << ",\n"
        << "      \"cancels\": " << run.mix.cancels << ",\n"
        << "      \"new_orders\": " << run.mix.new_orders << ",\n"
        << "      \"market_orders\": " << run.mix.market_orders << ",\n"
        << "      \"aggressive_limits\": " << run.mix.aggressive << ",\n"
        << "      \"cancel_pct\": "
        << (100.0 * static_cast<double>(run.mix.cancels) / static_cast<double>(opt.messages))
        << ",\n      \"versions\": [\n";
    for (std::size_t i = 0; i < run.results.size(); ++i) {
      const auto& r = run.results[i];
      out << "        {\n          \"name\": \"" << r.name << "\",\n"
          << "          \"throughput_per_sec\": " << static_cast<std::uint64_t>(r.throughput)
          << ",\n          \"speedup_vs_v0\": " << (r.throughput / base)
          << ",\n          \"events\": " << r.events
          << ",\n          \"mean_ns\": " << r.mean_ns
          << ",\n          \"p50_ns\": " << r.latency.percentile(50)
          << ",\n          \"p99_ns\": " << r.latency.percentile(99)
          << ",\n          \"p999_ns\": " << r.latency.percentile(99.9)
          << ",\n          \"max_ns\": " << r.latency.max()
          << ",\n          \"histogram\": [";
      const auto hist = r.latency.histogram();
      for (std::size_t j = 0; j < hist.size(); ++j) {
        out << "[" << hist[j].low << "," << hist[j].high << "," << hist[j].count << "]";
        if (j + 1 < hist.size()) out << ",";
      }
      out << "]\n        }" << (i + 1 < run.results.size() ? "," : "") << "\n";
    }
    out << "      ]\n    }" << (si + 1 < runs.size() ? "," : "") << "\n";
  }
  out << "  ]\n}";
  if (as_js) out << ";";
  out << "\n";
  std::cout << "wrote " << path << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const auto has_next = [&] { return i + 1 < argc; };
    if (!std::strcmp(argv[i], "--messages") && has_next())
      opt.messages = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--seed") && has_next())
      opt.seed = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--scenario") && has_next())
      opt.only = argv[++i];
    else if (!std::strcmp(argv[i], "--json") && has_next())
      opt.json = argv[++i];
    else {
      std::cerr << "usage: pokex-bench [--messages N] [--seed S]\n"
                   "                  [--scenario wide|mixed|deep] [--json out.json]\n";
      return 2;
    }
  }

  const bench::ClockFacts clock = bench::measure_clock();
  std::printf("\nsteady_clock here: ~%uns per call, ~%uns granularity.\n",
              clock.overhead_ns, clock.granularity_ns);
  std::printf("Percentiles are quantised to that granularity, so p50 sits on the clock's\n"
              "resolution floor and says little. Throughput and mean come from bulk timing\n"
              "with no per-call clock and are the numbers to trust. p99.9 and max resolve\n"
              "real outliers, including the pool and hash table doubling in size.\n");

  std::vector<ScenarioRun> runs;
  for (const Scenario& sc : kScenarios) {
    if (!opt.only.empty() && opt.only != sc.name) continue;
    ScenarioRun run;
    run.scenario = &sc;
    const auto flow = build_flow(opt, sc, run.mix);

    run.results.push_back(measure<BookV0Map>("v0  std::map + deque", flow));
    run.results.push_back(measure<BookV1Ladder>("v1  array ladder + bitmask", flow));
    run.results.push_back(measure<BookV2Pool>("v2  intrusive list + pool", flow));
    run.results.push_back(measure<BookV3Hash>("v3  open-addressing id index", flow));

    // Refuse to report anything if the versions disagree. A benchmark comparing
    // implementations that did different work is worse than no benchmark.
    for (const auto& r : run.results) {
      if (r.stream_hash == run.results.front().stream_hash) continue;
      std::cerr << "pokex-bench: FATAL: " << r.name << " produced a different event stream to "
                << run.results.front().name << " in scenario '" << sc.name
                << "'. Refusing to report timings.\n";
      return 1;
    }
    runs.push_back(std::move(run));
  }

  if (runs.empty()) {
    std::cerr << "pokex-bench: no scenario matched '" << opt.only << "'\n";
    return 2;
  }

  for (const auto& run : runs) {
    const auto& sc = *run.scenario;
    std::printf("\n\n=== %s: %s ===\n", sc.name, sc.description);
    std::printf("%zu resting orders across %zu price levels (~%.0f orders per level)\n",
                sc.resident, sc.levels,
                static_cast<double>(sc.resident) / static_cast<double>(sc.levels));
    std::printf("timed: %zu messages, %zu cancels (%.1f%%), %zu new "
                "(%zu market, %zu aggressive)\n",
                opt.messages, run.mix.cancels,
                100.0 * static_cast<double>(run.mix.cancels) / static_cast<double>(opt.messages),
                run.mix.new_orders, run.mix.market_orders, run.mix.aggressive);
    std::printf("all versions agreed: %zu events, hash %016llx\n\n",
                run.results.front().events,
                static_cast<unsigned long long>(run.results.front().stream_hash));

    std::printf("%-30s %13s %8s %9s %9s %8s %10s\n", "book version", "msgs/sec", "mean",
                "p99", "p99.9", "max", "vs v0");
    std::printf("%-30s %13s %8s %9s %9s %8s %10s\n", "------------", "--------", "----",
                "---", "-----", "---", "-----");
    const double base = run.results.front().throughput;
    for (const auto& r : run.results)
      std::printf("%-30s %13llu %6.0fns %7uns %7uns %6.1fus %9.2fx\n", r.name.c_str(),
                  static_cast<unsigned long long>(r.throughput), r.mean_ns,
                  r.latency.percentile(99), r.latency.percentile(99.9),
                  static_cast<double>(r.latency.max()) / 1000.0, r.throughput / base);
  }
  std::printf("\n");

  if (!opt.json.empty()) write_json(opt.json, opt, clock, runs);
  return 0;
}
