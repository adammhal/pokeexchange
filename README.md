# PokeExchange

A limit order book and matching engine in C++20, with price-time priority and a
deterministic replay tool. Eventually there will be a population of trading bots
and a live web UI on top, where every Pokemon species is a tradeable instrument.

This is v0. The engine is correct but deliberately slow, because the point of the
project is to make it fast later and be able to prove by how much.

## Status

Working now:

- Limit and market orders, single instrument
- Price-time priority, FIFO within a price level
- Partial fills with correct residual handling
- Cancel, and rejections for zero quantity, duplicate id, and unknown order
- Deterministic replay: same input bytes, same output bytes, any machine
- A Python reference implementation the C++ is diffed against

Not yet:

- IOC, FOK, post-only, modify, self-trade prevention, multiple instruments
- Any performance work at all. That is the next milestone, and doing it after
  the tests exist is the whole idea.

## Build and test

Needs CMake 3.20+, a C++20 compiler, and Python 3. Catch2 is fetched
automatically, so the first configure needs a network connection.

```sh
cmake -S . -B build
cmake --build build -j
cd build && ctest --output-on-failure
```

Run the engine directly:

```sh
printf 'N 1 S L 101 50\nN 2 S L 102 150\nN 3 B M 0 400\n' | ./build/pokex-replay
```

```
A 1 1
A 2 2
A 3 3
T 1 3 101 50 3     <- 50 filled at 101, the resting order's price
T 2 3 102 150 3    <- then 150 at 102, walking up the book
X 3 200            <- 200 left over, cancelled, nothing else to hit
```

With sanitizers:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DPOKEX_SANITIZE=ON
cmake --build build-asan -j
cd build-asan && ctest --output-on-failure
```

## How it is tested

Three layers, weakest to strongest:

1. **Golden tests.** Hand written scenarios with expected event streams. These
   pin down the semantics, like the rule that a trade prints at the maker's
   price and not the taker's.
2. **Property tests.** A million random messages, with invariants checked as it
   goes: the book never crosses, no order fills more than its size, and for
   every order `filled + cancelled + resting == original`. The last one is
   checked by comparing the book's own state against accounting derived purely
   from the emitted events, so the two views have to agree.
3. **Differential tests.** The C++ engine and the Python reference are run over
   10,000 random flows and the output has to be byte identical. Two independent
   implementations agreeing is much stronger evidence than any test suite I
   would have thought to write.

The invariant tests were also checked by breaking the engine on purpose in five
different ways (trading at the taker's price, serving newest first instead of
FIFO, forgetting to decrement a resting order, and so on) and confirming each
one gets caught. A test that has never failed has not been shown to work.

## Design decisions worth knowing

**Prices are integers, never floats.** A price is a count of ticks. Exact
equality is required for price-time priority, and `0.1` has no exact binary
representation, so with floats a price level can silently split in two. It also
means a price can be used directly as an array index later.

**No clock, no randomness, and no allocation-dependent iteration in the engine.**
Time is a counter that ticks once per accepted message. This buys determinism,
which is what makes replay testing, the Python diff, and an honest benchmark
possible at all. Timing instrumentation lives outside the engine.

**The book is swapped at compile time.** `MatchingEngine<Book>` is a template, so
the matching logic is written once and each book version plugs in underneath. A
virtual base class would have been the obvious move and the wrong one, since an
indirect call on the hot path would mean benchmarking the abstraction instead of
the data structure.

**Rejections are data, not exceptions.** The engine never throws and never logs.
Bad input produces a `Rejected` event and the book is untouched. A function that
can throw halfway through a match can leave the book half updated, and there is
no recovering from that.

## Layout

```
include/pokex/
  types.hpp           Price, Quantity, OrderId, Side, Order, Sequence
  events.hpp          commands in, events out
  book_concept.hpp    the five operations a book has to provide
  book_v0_map.hpp     std::map + std::deque. The slow baseline, kept forever
  matching.hpp        the matching loop, written once against the concept
  text_codec.hpp      the replay file format
src/replay_main.cpp   the CLI
reference/engine.py   independent Python implementation
tests/                golden, property, differential, determinism
docs/explainer/       a 41 page primer on how all of this works
docs/superpowers/     the design spec
```

## Roadmap

| | | |
|---|---|---|
| A1 | engine core | done |
| A2 | IOC, FOK, post-only, modify, self-trade prevention, multi-instrument | next |
| B | trading agents and order flow | |
| C | v1 to v3 optimisation, benchmarks, the actual latency numbers | |
| D | WebSocket gateway and web UI | |
| E | Avellaneda-Stoikov market maker, arbitrage bot, Pokemon fundamentals | |
| F | stylized facts analysis | |

## Reading

`docs/explainer/pokeexchange-explainer.pdf` explains the whole thing from
scratch, assuming no finance background: what an order book is, how matching
works, why memory layout ends up mattering more than big-O, and the maths behind
the agents that come later.
