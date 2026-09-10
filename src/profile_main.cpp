// pokex-profile: replays a fixed order flow through ONE book version and exits.
//
// It exists so a cache profiler measures the book rather than the measuring
// apparatus. pokex-bench runs all four versions in one process and keeps
// latency samples, histograms and hashes, all of which would show up in a
// cachegrind profile and none of which is the thing being studied. This does
// the flow and nothing else.
//
//   pokex-profile v0|v1|v2|v3 [--scenario wide|mixed|deep] [--messages N]
//                             [--resident R] [--levels L]
//
// Cachegrind is a simulator, so the workload has to be small enough to finish
// while still being large enough that the book does not fit in L1. A few
// hundred thousand orders over a few thousand levels satisfies both.
#include <cstdio>
#include <cstring>
#include <string>

// Instrumentation is gated to the steady-state phase only.
//
// This matters more than it looks. v1 allocates 65,536 price levels per side,
// so constructing one costs over a hundred thousand deque constructions before
// it has seen a single order. Profiling the whole process would fold that into
// the comparison and make v1 look far worse than it behaves once running,
// which is the opposite of what the measurement is for. Callgrind can be told
// to start collecting only when we say so; cachegrind cannot, which is why
// this uses callgrind with cache simulation turned on.
#if defined(__has_include)
#  if __has_include(<valgrind/callgrind.h>)
#    include <valgrind/callgrind.h>
#    define POKEX_HAVE_CALLGRIND 1
#  endif
#endif
#ifndef POKEX_HAVE_CALLGRIND
#  define CALLGRIND_START_INSTRUMENTATION do {} while (0)
#  define CALLGRIND_STOP_INSTRUMENTATION do {} while (0)
#  define CALLGRIND_ZERO_STATS do {} while (0)
#endif

#include "flow.hpp"
#include "pokex/all_books.hpp"
#include "pokex/matching.hpp"

using namespace pokex;

namespace {

template <typename BookT>
std::uint64_t replay(const bench::Flow& flow) {
  MatchingEngine<BookT> engine;
  std::uint64_t events = 0;
  const auto sink = [&events](const Event&) { ++events; };

  // Construction and book building: deliberately outside the measurement.
  for (const auto& c : flow.preload) engine.submit(c, sink);

  CALLGRIND_ZERO_STATS;
  CALLGRIND_START_INSTRUMENTATION;
  for (const auto& c : flow.steady) engine.submit(c, sink);
  CALLGRIND_STOP_INSTRUMENTATION;

  return events;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: pokex-profile v0|v1|v2|v3 [--scenario NAME] "
                 "[--messages N]\n");
    return 2;
  }
  const std::string book = argv[1];

  bench::FlowOptions opt;
  opt.messages = 100'000;  // small: cachegrind is a simulator, not a sampler
  const char* scenario_name = "mixed";
  std::size_t resident_override = 0;
  std::size_t levels_override = 0;

  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--messages") && i + 1 < argc)
      opt.messages = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--scenario") && i + 1 < argc)
      scenario_name = argv[++i];
    else if (!std::strcmp(argv[i], "--resident") && i + 1 < argc)
      resident_override = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--levels") && i + 1 < argc)
      levels_override = std::stoull(argv[++i]);
    else {
      std::fprintf(stderr, "pokex-profile: unexpected argument '%s'\n", argv[i]);
      return 2;
    }
  }

  const bench::Scenario* found = bench::find_scenario(scenario_name);
  if (found == nullptr) {
    std::fprintf(stderr, "pokex-profile: unknown scenario '%s'\n", scenario_name);
    return 2;
  }
  bench::Scenario scenario = *found;
  if (resident_override) scenario.resident = resident_override;
  if (levels_override) scenario.levels = levels_override;

  bench::Mix mix;
  const bench::Flow flow = bench::build_flow(opt, scenario, mix);

  std::uint64_t events = 0;
  if (book == "v0") events = replay<BookV0Map>(flow);
  else if (book == "v1") events = replay<BookV1Ladder>(flow);
  else if (book == "v2") events = replay<BookV2Pool>(flow);
  else if (book == "v3") events = replay<BookV3Hash>(flow);
  else {
    std::fprintf(stderr, "pokex-profile: unknown book '%s'\n", book.c_str());
    return 2;
  }

  std::fprintf(stderr,
               "%s %s: %zu resting over %zu levels, %zu messages -> %llu events\n",
               book.c_str(), scenario.name, flow.preload.size(), scenario.levels,
               flow.steady.size(), static_cast<unsigned long long>(events));
  return 0;
}
