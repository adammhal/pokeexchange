# Limit Order Book & Matching Engine — Sub-project A1 Design

**Date:** 2026-08-26
**Status:** approved (design), pending implementation plan
**Internal codename:** PokéExchange · **Resume name:** "Limit Order Book & Matching Engine"

---

## 1. Why this project exists

Three things quant firms probe that the current resume cannot demonstrate:

1. **C++ under latency constraints** — existing C++ experience is Arduino/ESP32 firmware only.
2. **Data-structure selection with measured consequences** — not "I know a hash map", but
   "I replaced X with Y, p99 fell from A to B, here is the profile."
3. **Market-microstructure vocabulary** — spread, depth, adverse selection, inventory
   risk, price-time priority. These cannot be faked in an interview.

The deliverable is a project with **a number that was earned rather than quoted**.

The Pokémon theme is a deliberate hook: memorable enough that an interviewer asks,
deep enough that the answer takes twenty minutes. It stays off the resume line and
comes out in conversation.

## 2. Scope decomposition

The original spec is 4 phases / 7 stages — realistically 6–10 weeks, too large for one
design. It is decomposed into six independently shippable sub-projects, each getting its
own spec → plan → build cycle.

| | Sub-project | Ships | Depends on |
|---|---|---|---|
| **A** | **Engine core** ← *this spec* | Book, order types, cancel/modify, Python reference, fuzz + determinism tests | — |
| B | Flow generator | Noise + momentum agents, terminal tape | A |
| C | Optimisation & benchmarks | v1–v3, latency harness, **the numbers** | A, B |
| D | UI & gateway | WebSocket feed, ladder, tape, candles, latency panel | A, B |
| E | Smart agents | Avellaneda–Stoikov maker, arbitrageur, Pokémon fundamentals | B |
| F | Analysis | Stylized facts, impact curve, P&L attribution, write-up | E |

Critical path to the mentor's Stage-3 checkpoint (throughput + latency numbers) is
**A → B → C**. D, E, F are not on it.

### A is split in two

A1's tests are what make A2 safe — invariant checks and the differential harness are
written *before* the fiddly order types they police.

- **A1 (this spec):** one instrument · limit + market orders · cancel · partial fills ·
  price-time priority · replay CLI · Python reference · determinism + fuzz tests.
  **Green, pushed, demoable.**
- **A2 (fast follow):** IOC · FOK · post-only · modify with priority semantics ·
  self-trade prevention · multiple instruments.

## 3. Core model

### Decision: prices are integers

A price is a signed 32-bit count of **ticks**, never a float. Tick size is display
metadata living outside the engine.

*Rationale:* price-time priority requires exact equality and exact ordering. `0.1` has no
exact binary representation, so with floats "same price level?" has a fuzzy answer and
the FIFO queue silently splits into two near-identical levels. Integers are also the
precondition for the v1 array-indexed ladder — an array cannot be indexed by `100.35`.

### Decision: no clock, no randomness, no allocation-dependent iteration in the engine

Time inside the engine is a `uint64` sequence counter incremented once per accepted
message. That counter establishes time priority. No `steady_clock`, no `rand()`, no
iteration over containers whose order depends on heap addresses. Timing instrumentation
lives *outside* the engine.

*Rationale:* this buys **determinism** — identical input bytes produce byte-identical
output bytes on any machine. Determinism is the precondition for the two most valuable
artifacts in the project:
- **Differential testing** against the Python reference (two independent implementations
  agreeing is stronger evidence than any hand-written suite).
- **An honest benchmark** — v0 and v3 can be proven to have done the same work, so the
  only thing that differed was speed.

### Commands and events

```
commands:  NewOrder{id, side, price, qty, type}   Cancel{id}
events:    Accepted{id, seq}    Rejected{id, reason}
           Trade{maker_id, taker_id, price, qty, seq}
           Canceled{id, unfilled_qty}
```

For a `Market` order the `price` field is ignored — it is not a sentinel value to be
compared against, and the matching loop must not read it. A `NewOrder` carrying
`type = Market` with any price whatsoever behaves identically. This is stated explicitly
because "market order = limit order at price 0 / INT_MAX" is a tempting shortcut that
breaks the moment negative tick prices are allowed.

### Decision: trades print at the maker's price

A resting ask at 101 hit by a limit buy at 105 trades at **101**; the buyer receives 4
ticks of price improvement.

*Rationale:* the resting order published its terms in public and waited; the aggressor
read them and accepted. You do not charge somebody more than they advertised because you
discovered they would have paid it. (This is the single most common bug in a first
matching engine.)

### Matching algorithm

```
incoming BUY order:
  while remaining > 0 and best_ask exists and best_ask <= limit_price:
      resting = front of level[best_ask]        # front == oldest == time priority
      fill    = min(remaining, resting.remaining)
      emit Trade{maker: resting, taker: incoming, price: best_ask, qty: fill}
      decrement both; pop resting if exhausted; drop level if empty
  if remaining > 0:
      Limit  -> insert at back of level[limit_price]
      Market -> emit Canceled{remaining}
```

**A1 policy:** a market order with unfillable remainder has that remainder cancelled
(not held). Documented, not silent.

### The five operations that are the whole engineering project

```cpp
best(Side)      -> optional<Price>   // top of book
level(Price)    -> Level&            // FIFO queue at a price
insert(Order)   -> Handle            // rest an order, back of queue
find(OrderId)   -> Handle            // for cancel — ~90% of real traffic
remove(Handle)  -> void
```

The matching loop is written once and never meaningfully changes. v0–v4 are five attempts
to make these five operations faster, and nothing else.

## 4. Decision: compile-time seam, not virtual dispatch

`template<typename Book> class MatchingEngine`. Matching logic written once against a
documented Book concept; each version is a separate class satisfying it.

*Rationale:* a virtual call is an indirect jump that defeats inlining and pollutes the
branch predictor — using one here would mean measuring our own abstraction rather than
the data structure, in the very benchmark the project exists to produce. Zero runtime
cost, and the same test suite instantiates against every version, which prevents four
implementations from silently drifting apart and quietly invalidating the comparison.

*Cost accepted:* template diagnostics are poor, and the Book interface must be pinned
down precisely up front.

*Rejected:* separate full implementations per version (copy-pasted matching logic — a
semantics bug must be fixed four times, and versions drift). Virtual `IBook` base class
(indirect call on the measured hot path).

## 5. Architecture

```
libpokex/                  static lib · no I/O, no threads, no clock
  types.hpp                Price, Qty, OrderId, Side, Order, Sequence
  events.hpp               commands + events, and their text encoding
  book_concept.hpp         the five operations, documented as a contract
  book_v0_map.hpp          std::map<Price, deque<Order>> — baseline, kept forever
  matching.hpp             template<Book> — the loop, written ONCE

pokex-replay               CLI: commands.txt -> events.txt. Pure. Deterministic.
reference/engine.py        independent reimplementation, same file formats
tests/                     Catch2 v3: golden · property/fuzz · differential
```

**Data flow is a straight line with no side channels.** A text command file goes into
`pokex-replay`, which drives `MatchingEngine<BookV0>` one command at a time and appends
each emitted event to an output file. That is the only way the engine is exercised in A1
— no sockets, no threads, no database.

*Rationale:* a pure `commands → events` function is trivially testable, reproducible and
benchmarkable. Every later sub-project (agents, WebSocket gateway, SPSC ring buffer)
attaches *around* that function without modifying it.

**Text file formats, not binary,** for A1 — human-readable diffs are worth more than
parse speed during development, and the parser sits outside the engine so it never
touches a latency number.

### Error handling: rejections are data

The engine never throws and never logs. Bad input produces a `Rejected` event with a
reason code; the book is left untouched.

*Rationale:* a function that can throw mid-match can leave the book half-updated, and a
half-updated book is unrecoverable — the audit trail is gone, which was the point. An
error path that is *data* is exercised by the same tests as everything else; an error path
that is an exception is the code nobody tests. Malformed input is not exceptional in a
system open to the public.

Invariant violations indicating an engine *bug* (as distinct from bad input) hit an
`assert` in debug builds and are the fuzz harness's job to find.

## 6. Testing strategy

Three layers, weakest to strongest:

1. **Golden tests.** Hand-written scenarios with expected event streams. Catch semantic
   mistakes (maker-price rule, residual handling) and double as behaviour documentation.
2. **Property / fuzz tests.** Random *valid* command sequences; assert invariants after
   every single message:
   - the book never crosses (`best_bid < best_ask`)
   - no order fills more than its size
   - per order: `filled + resting + cancelled == original`
   - within a price level, fills occur in insertion order
3. **Differential testing.** C++ engine and Python reference on the same random flows,
   byte-identical output required.

Plus: replay determinism (same file twice → identical bytes) and clean runs under
AddressSanitizer and UndefinedBehaviorSanitizer.

## 7. Definition of done for A1

- [ ] All tests green, including a fuzz run of ≥ 1,000,000 messages
- [ ] Same input file replayed twice produces byte-identical output
- [ ] C++ and Python agree on ≥ 10,000 randomly generated flows
- [ ] Clean under ASan and UBSan
- [ ] README a stranger can follow to build and run

**Explicitly not on the list: any performance number.** A1 is allowed to be slow. Making
it fast is sub-project C, and doing it in that order is what makes the eventual speed
claim believable rather than decorative.

## 8. Build environment

- C++20, CMake, Catch2 v3 via `FetchContent`
- `-Wall -Wextra -Wpedantic`, plus a sanitizer build type
- Toolchain present: Apple clang 21, CMake, Python 3 (numpy). Ninja absent — use Make,
  or install Ninja (optional).

## 9. Known constraint, deferred to sub-project C

The development machine is Apple silicon. `rdtsc` is an x86 instruction (the ARM
equivalent is reading `cntvct_el0`, which ticks at a different and lower frequency), and
`perf stat` — the source of cache-miss and branch-misprediction counts — is Linux-only.
macOS offers `xctrace`, which is considerably less direct.

This does not block A1; the timing abstraction is kept outside the engine so it cannot
contaminate the design. **Decision required before C:** a cheap Linux cloud box, a local
VM, or accept weaker profiling evidence on macOS. **Recommendation: the Linux box** —
`perf stat` output showing the cache-miss rate falling between v0 and v1 is one of the
most persuasive artifacts this project can produce.

## 10. The bullet this is all for

> Built a limit order book and matching engine in C++; replaced a `std::map` book with an
> array-indexed price ladder, an intrusive free-list allocator and an open-addressing ID
> index, taking sustained throughput from X to Y orders/sec and p99 matching latency from
> A to B µs on an identical replayed order flow, verified byte-identical against a Python
> reference implementation.

Every number in it is measured; every claim is one the tests can prove.

## 11. Companion document

`docs/explainer/pokeexchange-explainer.pdf` (41pp, 11 figures) — a first-principles
explainer covering the domain, the microstructure vocabulary, and the mathematics for
Phases 2–4 (Avellaneda–Stoikov, market impact, stylized facts, cointegration, Sharpe).
Generated reproducibly by `docs/explainer/build.py`.
