# Sub-projects C1, B1, D1 — Optimization ladder, agent flow, and UI

**Date:** 2026-08-27
**Status:** approved
**Follows:** `2026-08-26-matching-engine-a1-design.md`

Three independently shippable pieces, in dependency order. A UI with no order
flow shows an empty book, so the agents come before the UI even though the
request led with v1.

| | Ships | Depends on |
|---|---|---|
| **C1** | v1, v2, v3 books + benchmark harness | A1 |
| **B1** | Agents + recorded session file | A1 |
| **D1** | Static UI + screenshots + GitHub Pages | C1, B1 |

---

## C1 — The optimization ladder

Each version isolates exactly one change, so a measured gain is attributable to
a specific cause rather than to a rewrite.

| | Change | Cancel path afterwards |
|---|---|---|
| v0 | `std::map<Price, std::deque<Order>>` | hash to (side, price), linear scan the deque |
| v1 | levels become an array indexed by tick; occupancy bitmask for best-price tracking | unchanged: still a linear scan |
| v2 | queues become intrusive doubly-linked lists over a preallocated object pool | still walks the level's list |
| v3 | open-addressing hash from `OrderId` to a node index | one probe, then O(1) unlink |

**The expected shape of the result is part of the design.** v1 and v2 should
barely improve cancel, which is roughly 90% of real traffic; v3 should improve it
a lot. That progression is more informative than four numbers that all improve
for unclear reasons, and it is the thing to be able to explain.

### v1: array-indexed ladder

- `std::array<std::deque<Order>, kPriceLevels>` per side. Only the *level lookup*
  changes in v1; the per-level queue stays a `deque` so the change under test is
  isolated to "tree versus array".
- Occupancy tracked in a bitset of `kPriceLevels / 64` words. Best bid is the
  highest set bit, best ask the lowest, found with `std::countl_zero` /
  `std::countr_zero` from a cached hint rather than by scanning from the end.
- Cancel keeps the v0 approach (`unordered_map<OrderId, Location>` then scan).

### v2: intrusive list + object pool

- `Node { Order order; std::uint32_t next, prev; }` held in a preallocated
  `std::vector<Node>`, with a free list threaded through `next`. Steady-state
  operation performs no allocation.
- A level is `{ std::uint32_t head, tail; }` holding pool indices, not pointers,
  so the pool can be reserved once and never reallocated.
- Cancel still resolves (side, price) by hash and then walks that level's list.

### v3: open-addressing id index

- Linear-probing hash table, power-of-two capacity, mapping `OrderId` directly to
  a pool index. Backward-shift deletion rather than tombstones, so the table does
  not degrade over a long run of cancels.
- Chosen over `std::unordered_map` because that is a linked list per bucket,
  which reintroduces exactly the pointer chasing v1 and v2 removed.

### Bounded price domain (changes existing A1 behaviour)

An array indexed by price requires a bounded domain. `types.hpp` fixes
`kMinPrice = 1` and `kMaxPrice = 65535`, and **the engine rejects out-of-range
prices for every book, including v0**, with a new
`RejectReason::PriceOutOfRange`.

*Rationale:* enforcing the bound only inside v1 would make v0 and v1 disagree on
out-of-range input, which would break cross-version equivalence, which is the
property the whole benchmark claim rests on. The Python reference learns the same
rule.

### Benchmark harness

`pokex-bench` replays one fixed-seed flow through all four books and asserts the
four event streams are byte-identical, so the comparison is of speed and nothing
else. Reports throughput plus p50/p99/p99.9/max from a log-bucketed histogram
written in-tree rather than pulling in HdrHistogram.

Two honesty requirements:

- **Publish the timer's own overhead.** `steady_clock` costs roughly 20-30ns per
  call on this machine, a large fraction of a ~100ns operation. Quoting
  percentiles without disclosing that would be misleading.
- **Throughput is measured in bulk** (total elapsed / N), which carries no
  per-call timer overhead, so it is the cleaner headline figure.

Writes `bench.json` for the UI's latency panel.

---

## B1 — Agents and the recorded session

`pokex-sim` owns a virtual clock and a seeded PRNG. The engine remains clockless
and deterministic; the simulator advances time. Same seed produces an identical
session file, which is asserted by a test.

Four agents:

- **Noise trader** — Poisson arrivals, random direction. Provides base flow.
- **Momentum** — trades in the direction of a short-versus-long EMA crossover.
- **Mean reversion** — fades deviation from the longer EMA.
- **Simple market maker** — quotes both sides around the mid with an inventory
  skew, and re-quotes every tick. This is what makes the traffic realistically
  cancel-heavy, which is what makes the v3 result matter.

Full Avellaneda-Stoikov is deliberately deferred to its own piece of work; it
needs a live volatility estimate and parameter tuning to avoid pathological
behaviour, and it deserves the attention.

Output is one `session.json`: periodic book snapshots, the trade tape, and
per-agent P&L and inventory, capped near 1200 frames so GitHub Pages can serve
it.

---

## D1 — UI

One static page, vanilla JS, canvas panels. No build step, no bundler, no
`node_modules`, so it deploys to Pages as-is and cannot break CI.

Panels: order book ladder (animated), cumulative depth curve, candles with
volume, trade tape, agent leaderboard, latency histogram, and playback controls
with a scrubber.

Dark trading-terminal theme using the same validated bid-blue / ask-red palette
as the explainer PDF, so the artifacts read as one project. Blue and red rather
than the conventional green and red because green/red is the worst pair for
red-green colour vision deficiency.

Data arrives as a recorded `session.json` replayed in the browser rather than
over a socket. That keeps the demo hostable as a static page, which is what
allows a clickable link in the README. A WebSocket source can be swapped in
later behind the same event schema when manual trading mode is wanted.

---

## Testing

- **All four books run the identical suite** via Catch2 `TEMPLATE_TEST_CASE`, so
  the golden, property, and fuzz tests cover every version rather than only v0.
- **Cross-version equivalence:** v0, v1, v2 and v3 produce identical event
  streams on the same random flows.
- **Python differential** extended with a `--book` flag.
- **Sim determinism:** same seed produces an identical session file.
- Everything under AddressSanitizer and UndefinedBehaviorSanitizer in CI.

## Definition of done

- [ ] All four books pass the shared suite, including a 1,000,000-message fuzz run
- [ ] Cross-version event streams byte-identical
- [ ] `pokex-bench` reports throughput and percentiles for all four, with timer
      overhead disclosed
- [ ] `pokex-sim` deterministic across runs for a fixed seed
- [ ] UI renders a recorded session; screenshots in the README; Pages deploy green
- [ ] Zero compiler warnings; clean under ASan and UBSan
