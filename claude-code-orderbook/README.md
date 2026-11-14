# C++20 Limit Order Book & Matching Engine

A single-instrument limit order book with a price-time priority matching engine,
a reproducible synthetic order-feed generator, a replay system, an 84-case test
suite, and a benchmark + profiling harness.

The goal is a **correct, readable baseline** built from standard containers,
measured honestly, with the bottlenecks identified from data. No performance
optimizations have been applied yet — see [Recommended optimizations](#recommended-optimizations).

---

## Contents

- [Features](#features)
- [Project layout](#project-layout)
- [Building](#building)
- [Running the tests](#running-the-tests)
- [Running the benchmarks](#running-the-benchmarks)
- [Running the profiler](#running-the-profiler)
- [Design](#design)
- [Benchmark results](#benchmark-results)
- [Bottleneck analysis](#bottleneck-analysis)
- [Recommended optimizations](#recommended-optimizations)
- [Limitations and assumptions](#limitations-and-assumptions)

---

## Features

- Limit and market orders, buy and sell
- Partial and complete fills; an order can sweep multiple price levels
- **Price-time priority**: better price first, then earliest arrival
- Cancellation by order ID (average O(1))
- Trade records: buy ID, sell ID, execution price, quantity, aggressor side
- Best bid / best ask / market depth (N levels, each with total quantity and order count)
- Integer prices in ticks (`int64_t`); no floating point anywhere
- Input validation: zero quantity, quantity above cap, duplicate live ID
- Seeded synthetic feed generator, identical output on every compiler
- Replay engine with an order-sensitive trade checksum; CSV save/load of feeds and trades
- Benchmarks: throughput plus per-event P50/P95/P99/P99.9 latency at 10k / 100k / 1M events, 4 workloads

## Project layout

```
claude-code-orderbook/
├── CMakeLists.txt
├── README.md
├── include/orderbook/
│   ├── types.hpp             Price, Quantity, OrderId, Side, OrderType
│   ├── order.hpp             Order
│   ├── trade.hpp             Trade (execution record)
│   ├── event.hpp             Event = Add(order) | Cancel(id): one feed message
│   ├── order_book.hpp        OrderBook: resting orders, best bid/ask, depth
│   ├── matching_engine.hpp   MatchingEngine: validation + matching rules
│   ├── feed_generator.hpp    FeedConfig + workload presets, generateFeed()
│   └── replay.hpp            applyEvent(), replay(), ReplayStats, CSV I/O
├── src/                      implementations of the above
├── tests/
│   ├── test_framework.hpp    ~70-line test harness (TEST_CASE / CHECK / CHECK_EQ / REQUIRE)
│   ├── test_helpers.hpp      printing for Order/Trade/etc., limit()/market() builders
│   ├── test_order_book.cpp   15 tests: the container alone
│   ├── test_matching.cpp     23 tests: matching, fills, priority, market orders
│   ├── test_cancel_edge.cpp  17 tests: cancellation, validation, extreme values
│   ├── test_reference_model.cpp  3 tests: random comparison vs. a simple reference engine
│   ├── test_generator_replay.cpp 22 tests: generator, replay, CSV
│   └── test_latency_stats.cpp     4 tests: percentile math used by the benchmark
└── bench/
    ├── cycle_timer.hpp       rdtsc timer + calibration + invariant-TSC check
    ├── latency_stats.hpp     nearest-rank percentiles, median
    ├── benchmark_main.cpp    orderbook_bench: throughput + latency
    └── profile_main.cpp      orderbook_profile: per-event-class cost, allocations, page faults
```

CMake targets: `orderbook_core` (static library), `unit_tests`, `orderbook_bench`, `orderbook_profile`.

## Building

Requirements: CMake ≥ 3.20 and a C++20 compiler (developed with MSVC 19.29 /
Visual Studio 2019 16.11; the code is standard C++20 and should also build with
recent GCC and Clang).

### Windows, Visual Studio 2019 (as used for the results below)

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release      # use Release for any performance measurement
cmake --build build --config Debug        # assertions enabled; use for development
```

Visual Studio is a multi-config generator: binaries go to `build\Release\` or `build\Debug\`.

> **If CMake reports `could not find any instance of Visual Studio`**: on this
> machine the VS 2019 Build Tools install is flagged "incomplete" by the Visual
> Studio Installer, even though the compiler, MSBuild and Windows SDK are all
> present, so CMake skips it. Either run **Repair** on Build Tools in the
> Visual Studio Installer, or point CMake at the install explicitly (this is
> stored only in `build/CMakeCache.txt`):
>
> ```powershell
> cmake -S . -B build -G "Visual Studio 16 2019" -A x64 `
>   "-DCMAKE_GENERATOR_INSTANCE=C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools,version=16.11.36602.28"
> ```

### Linux / macOS (Ninja or Makefiles)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # single-config generators default to Release
cmake --build build -j
```

All targets build with no warnings at `/W4 /permissive-` (MSVC) or `-Wall -Wextra -Wpedantic`.

## Running the tests

```powershell
.\build\Debug\unit_tests.exe              # all 84 tests
.\build\Debug\unit_tests.exe match:       # only tests whose name contains "match:"
ctest --test-dir build -C Debug           # same suite via CTest
```

Test name prefixes: `book:`, `match:`, `cancel:`, `edge:`, `reference:`, `gen:`,
`replay:`, `csv:`, `stats:`. The process exits non-zero if any test fails, and
each failure prints the file, line, expression, and both values. CSV tests write
only to `build/test_output/`.

Every test checks **both** the trades produced and the resulting book. Highlights:

- **Hand-written cases** for every requirement: insertion, same-price matching,
  price priority on each side, time priority (including a partially filled
  order keeping its place in line), partial and complete fills, multi-level
  sweeps, market orders (including into an empty book or larger than the
  book), cancels of live / filled / unknown / already-cancelled IDs, the empty
  book, `INT64_MIN`/`INT64_MAX` prices, maximum quantities, and 1,000
  maximum-size orders at one level (no overflow).
- **Randomized comparison test**: 60,000 random operations (15 seeds; tight,
  wide and cancel-heavy order flow) go through both the real engine and a
  deliberately naive reference engine (flat vector, linear scan). Trades,
  execution reports, cancel results and *full depth on both sides* must match
  after every operation. I checked that it works by planting a one-character
  bug in the matching code: it was caught at operation 22.
- **Golden fingerprint**: the seed-42 balanced feed is pinned to a fixed hash,
  so any change to the generator's output fails a test (otherwise old and new
  benchmark results could not be compared).

## Running the benchmarks

```powershell
.\build\Release\orderbook_bench.exe                                    # 4 workloads × 10k/100k/1M, 5 repeats
.\build\Release\orderbook_bench.exe --sizes 100000 --workloads wide-book --repeats 10
.\build\Release\orderbook_bench.exe --csv build\results.csv            # also write results as CSV
.\build\Release\orderbook_bench.exe --help
```

Options: `--sizes N,N,...`, `--workloads balanced,cancel-heavy,aggressive,wide-book`,
`--seed S` (default 42), `--repeats R` (default 5), `--csv path`.

**On hybrid CPUs (Intel 12th gen and later, Core Ultra), pin the benchmark to a
performance core.** On the test machine, the same benchmark ran 2–2.5× slower
when Windows placed it on an efficiency core. Logical CPU 16 was a fast core on
this machine; check yours by comparing a couple of masks:

```powershell
cmd /c "start /affinity 10000 /wait /b `"`" build\Release\orderbook_bench.exe"
```

### What is measured, and how

For each (workload, size):

| Phase | Timed? | Notes |
|---|---|---|
| 1. Generate feed | separately (`gen ms`) | all events built before any timing |
| 2. Warm-up replay | no | throwaway engine |
| 3. Throughput × R | `setup ms` (engine construction + `reserve`) and `replay ms` (loop only) | fresh engine per repeat; engine teardown happens after the clock stops; **median** reported, min/max shown |
| 4. Latency run | per event | rdtsc around each event; samples go into a preallocated array; percentiles computed afterward |

- The timed loops do no logging, no I/O, and no allocation by the harness
  (the trade buffer is reserved once). Allocations made *by the engine itself*
  are measured, because they are part of its cost.
- Every repeat must reproduce the same trade checksum, or the program aborts.
  The checksum also prevents the compiler from optimizing work away.
- **Latency clock**: on Windows, `steady_clock` usually ticks every 100 ns, too coarse
  for events of about 100 ns. The benchmark uses the CPU timestamp counter
  (`lfence; rdtsc` / `rdtscp; lfence`), calibrated against `steady_clock` at
  startup, and checks the CPU reports an invariant TSC. The timer's own cost
  (~14 ns on the test machine) is printed and is **included** in every latency sample.
- A Debug build prints a warning banner; only use Release numbers.

### Workloads

All centered on 10,000 ticks with quantities 1–100.

| Preset | Cancels | Market | Limit orders priced to cross | Price range | What it stresses |
|---|---|---|---|---|---|
| `balanced` | 20% | 5% | 10% | ±50 | typical mix, growing book |
| `cancel-heavy` | 45% | 2% | 5% | ±50 | cancel path (45% of all messages) |
| `aggressive` | 10% | 15% | 40% | ±10 | matching: ~0.73 trades per event |
| `wide-book` | 20% | 2% | 5% | ±2,000 | thousands of price levels |

Cancels target random IDs the feed has already issued, so some arrive after the
order has filled and return "not found", as in a real feed. The cancel-heavy rate is
deliberately below 50%. At 60% the feed cancelled orders as fast as it added
them, and the book stayed nearly empty (1 order at the end), which would have
benchmarked a trivial book.

## Running the profiler

```powershell
.\build\Release\orderbook_profile.exe            # 4 workloads × 100k and 1M
.\build\Release\orderbook_profile.exe 50000      # one custom size
```

A separate executable, because it replaces the global `operator new` to count
allocations. It never affects `orderbook_bench` numbers. For each workload it
reports, per event class (rest at existing level, rest creating a new level,
crossing limit, market, cancel hit, cancel miss): count, share of total time,
avg/P50/P99 latency, and allocations per event. It also reports allocation sizes
(which reveal real container node sizes), page faults during the replay, which
event classes the slowest 0.1% belong to, and a `malloc`/`free` reference cost.

## Design

### Components

- **`OrderBook`** stores resting limit orders and answers queries. It has no
  matching rules. The engine drives it through `bestOrder(side)` and
  `reduceBestOrder(side, qty)`.
- **`MatchingEngine`** owns the book and implements validation and the matching
  rules. `submit(order, trades&)` *appends* trades to a caller-owned vector
  (never clears it), so a hot loop can reuse one pre-reserved buffer.
- **`generateFeed`** produces a `std::vector<Event>`; **`replay` / `applyEvent`**
  apply events to an engine and accumulate `ReplayStats` (counts, volume, checksum).

### Data structures

```cpp
struct PriceLevel { std::list<Order> orders; Quantity totalQty; };   // FIFO: front = oldest

std::map<Price, PriceLevel, PriceCompare> bids_{PriceCompare{true}};  // begin() = highest bid
std::map<Price, PriceLevel, PriceCompare> asks_{PriceCompare{false}}; // begin() = lowest ask

struct Locator { Side side; LevelMap::iterator level; std::list<Order>::iterator order; };
std::unordered_map<OrderId, Locator> index_;                          // id → exact position
```

| Choice | Why | Cost (measured: see analysis) |
|---|---|---|
| `std::map` per side | Sorted; best price = `begin()` in O(1); new level O(log P); iterators stable | One heap node (64 B) per level; tree walk with unpredictable branches; poor locality with many levels |
| One comparator type with a `descending` flag | Bids and asks share **one map type**, so a single `Locator` can point into either side | One always-predicted branch per comparison |
| `std::list<Order>` per level | FIFO gives time priority; O(1) erase from the middle through a stored iterator | One 56 B heap node per order (+ MSVC allocates a sentinel per list) |
| `unordered_map` ID → `Locator` | Cancel never searches: O(1) average lookup, O(1) unlink, O(1) amortized empty-level erase via the stored map iterator | One 48 B heap node per order + 16 B per bucket in MSVC; ~2 cache misses per lookup on large books |
| `totalQty` maintained per level | Depth queries are a walk over N levels, no summing | 8 B per level |
| Integer ticks | Exact comparisons | Callers convert from currency |

### Matching algorithm

```
submit(order):
  reject if qty == 0, qty > 2^40, or id is already resting      (book unchanged)
  while order.qty > 0:
      resting = book.bestOrder(opposite side)                    // best price, then oldest
      stop if none, or (limit order and price doesn't cross)
      fill = min(order.qty, resting.qty)
      emit Trade{buyId, sellId, price = resting.price, fill, aggressor = order.side}
      book.reduceBestOrder(opposite, fill)                       // removes order/level at zero
  remainder: limit → rests at its price (back of queue); market → cancelled
```

Costs: O(1) per fill; resting is O(1) at an existing level or O(log P) for a new
one; cancel is O(1) on average. The trade record is built *before*
`reduceBestOrder`, because that call can remove the resting order and invalidate the pointer.

**Quantity cap (2⁴⁰ ≈ 1.1 × 10¹²)**: a level's `totalQty` is a `uint64`. With
this cap, overflowing it would take more than 2²⁴ (16.7M) maximum-size orders at a single price.

## Benchmark results

**Machine**: Intel Core Ultra 7 155H (hybrid, 22 logical CPUs), 15.4 GB RAM, Windows 11.
**Build**: Release, MSVC 19.29 (`/O2`), x64. **Run**: pinned to logical CPU 16,
seed 42, 5 throughput repeats (median). Measured 2026-11-14.

| Workload | Events | Events/s | ns/event | P50 | P95 | P99 | P99.9 | Book at end |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| balanced | 10k | 9.27 M | 107.9 | 115 | 196 | 282 | 4,849 | 4,141 orders, 92 levels |
| balanced | 100k | 8.58 M | 116.5 | 115 | 212 | 309 | 591 | 41,182 / 93 |
| balanced | 1M | 3.33 M | 300.0 | 248 | 583 | 768 | 1,458 | 412,120 / 91 |
| cancel-heavy | 10k | 13.91 M | 71.9 | 104 | 184 | 280 | 546 | 591 / 98 |
| cancel-heavy | 100k | 12.40 M | 80.7 | 106 | 244 | 311 | 491 | 7,099 / 96 |
| cancel-heavy | 1M | 5.91 M | 169.1 | 218 | 349 | 449 | 704 | 68,800 / 100 |
| aggressive | 10k | 10.30 M | 97.1 | 106 | 224 | 312 | 454 | 1,403 / 11 |
| aggressive | 100k | 9.71 M | 102.9 | 108 | 226 | 311 | 445 | 13,705 / 13 |
| aggressive | 1M | 5.75 M | 173.8 | 200 | 399 | 582 | 1,096 | 136,819 / 11 |
| wide-book | 10k | 6.64 M | 150.5 | 170 | 260 | 334 | 2,325 | 5,128 / 2,848 |
| wide-book | 100k | 5.28 M | 189.4 | 180 | 274 | 355 | 635 | 51,171 / 3,723 |
| wide-book | 1M | 2.80 M | 356.9 | 341 | 606 | 749 | 2,563 | 509,407 / 3,730 |

Latencies are in ns per event and include ~14 ns of timer cost. Not counted in
these numbers: feed generation 0.3–33 ms, engine setup 0.06–3.2 ms. The
**average** latency is left out of the table on purpose. Rare stalls lasting
0.1–17 ms (most likely the OS interrupting the process; not individually
attributed) distort it. For example, cancel-heavy at 10k averages 228 ns with a
104 ns P50. `orderbook_bench` still prints the average and maximum.

Trade checksums for these runs (they must stay identical after any optimization):

| Workload | 10k | 100k | 1M |
|---|---|---|---|
| balanced | `f6127b9f3c8fe9c6` | `63efd2051570f7ce` | `2e2503422380f5d9` |
| cancel-heavy | `8f3df79908773c27` | `f1b70d2dd8babc83` | `cb10d3edf79f8c15` |
| aggressive | `5f5fb21ff5a05e63` | `88877c27677944c1` | `7961f72dfd4cebcb` |
| wide-book | `c494e16ce3485f6d` | `cfa53a779bed8395` | `594c852188dcc48b` |

Unpinned runs gave the same checksums and similar numbers most of the time.
Their run-to-run spread was larger (one balanced-10k run reached 175 ns/event
against the usual ~105), consistent with occasionally running on an efficiency core.

## Bottleneck analysis

All figures are from `orderbook_profile` (Release).

**1. Two heap allocations for every resting order.** The allocation sizes match the container nodes exactly:

| Size | Node | When |
|---|---|---|
| 56 B | `std::list` node (16 B links + 40 B `Order`) | every resting order; plus every new level (MSVC list sentinel) |
| 48 B | `unordered_map` node (16 B links + 32 B id/locator) | every resting order |
| 64 B | `std::map` node | every new price level |

Resting at an existing level = **2.00 allocations**; creating a new level = **4.00**.
A reference measurement on this machine gives `malloc(64)` ≈ **41 ns** and
`free` ≈ **23.5 ns**. By that estimate, roughly 80 of the 148 ns to rest an order
(balanced, 100k) is allocator work. Resting orders account for 55–80% of
total time in every workload.

**2. The 1M slowdown is caused by memory size, not by the algorithm.** At 1M events the
balanced book ends with 412k orders: about 112 B per order plus a 16 MB bucket
array, ~60 MB in total, well beyond the 24 MB L3 cache. At 100k it is ~7 MB and
fits. Cancels show it most clearly:

| | 100k | 1M |
|---|---:|---:|
| cancel miss (only a hash lookup) | 53 ns | 175 ns (3.3×) |
| cancel hit (lookup + list unlink + hash unlink + free) | 114 ns | 535 ns (4.7×) |

A successful cancel follows about 5–6 pointers to unrelated memory: the bucket,
the hash node and its neighbors, the list node and its neighbors. On a large book
each is likely a cache miss. (This also means the 1M results partly reflect a
property of the synthetic feeds: their books keep growing for the whole run.)

**3. Wide books pay for finding the price level.** Resting at an existing level costs **392 ns**
in wide-book versus **148 ns** in balanced (both 100k). Both make exactly 2.00
allocations and have similar order counts. The difference is
`std::map::try_emplace` walking ~12 tree nodes across 3,723 levels (versus ~7
for 93), with an unpredictable branch at each, plus the end of a rarely used
level's queue usually being out of cache.

**4. Tail latency.** 13–16 page faults per 1,000 events (balanced, wide-book) come
from new heap pages being touched for the first time; the slowest 0.1% of events
are mostly ones that allocate. Stalls of 0.1–17 ms are much larger than any
engine operation and are almost certainly OS interrupts or preemption. That is
inferred, not proven per spike.

**5. The matching loop is not the bottleneck.** Crossing limit orders and
market orders cost about the same as resting orders, and stay under 40% of total
time even in the aggressive workload.

## Recommended optimizations

In order of expected impact. **These are recommendations, not implemented, and
the expected effects are estimates from the measurements above.**

1. **Order pool with an index-linked queue** (replacing `std::list<Order>`). Preallocate
   `Order` records, each with 32-bit `prev`/`next` indices; each level keeps
   head/tail indices; use a free list for reuse. This removes the 56 B
   allocation per order and the sentinel per level, cuts per-order memory from
   ~112 B to ~48 B (about twice as many orders fit in cache), and lets setup
   touch the memory in advance so page faults leave the timed loop.
2. **Flat open-addressing hash table for ID lookup** (replacing `std::unordered_map`):
   no node allocation, no 16 B per bucket overhead, and about one cache miss per lookup. If the
   engine assigned its own sequential order IDs (as real exchanges do), a plain
   `vector` indexed by ID would do. Together with #1: **zero allocations in
   steady state**, and it targets the 175–535 ns cancels at 1M.
3. **Direct price-to-level lookup.** For a bounded price band, keep levels in an array
   indexed by `price − base`, with a bitmap to find the next non-empty level
   when the best one empties. A lower-risk first step is to keep `std::map` but
   add a price → level hash table, so adding to an *existing* level never walks
   the tree. Targets wide-book's extra ~240 ns per resting order.
4. **Benchmark method**: pin to a performance core, lead with P50/P99, optionally
   count page faults per run, and use more repeats for 10k sizes.

**How to check an optimization**: all 84 tests must still pass (including the
60,000-operation comparison against the reference engine), and
`orderbook_bench` must reproduce every checksum in the table above.

## Limitations and assumptions

- One instrument, single-threaded; no networking or message protocol.
- Trades always execute at the **resting** order's price.
- Market orders are immediate-or-cancel: any unfilled remainder is cancelled, never rested.
- No self-trade prevention, order modification, hidden/iceberg, stop, or time-in-force types.
- Order IDs must be unique among *resting* orders. An ID can be reused once
  its order has been filled or cancelled.
- Quantities are capped at 2⁴⁰ per order. Prices may be any `int64_t`, including negative.
- The feed generator does not simulate matching, so some generated cancels target
  orders that have already filled. Each feed has a fixed mid price, so its books
  grow for the whole run.
