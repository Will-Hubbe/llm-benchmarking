# Simple C++ Limit Order Book

This C++20 project implements a single-instrument limit order book, a matching
engine, deterministic synthetic market-feed generation, event replay, tests,
and a small replay benchmark.

## Order book behavior

Buy and sell limit orders are stored at integer prices. Incoming orders match
the best available opposing price first and then FIFO order within each price.
Trades execute at the resting order's price. An incoming limit order may fill
partially and rest with its remainder; market orders execute against available
liquidity and never rest. Orders can be canceled by ID, and the book provides
best bid, best ask, and aggregate quantity-at-price queries.

The implementation uses a descending `std::map` of bid prices, an ascending
`std::map` of ask prices, `std::list` FIFO queues at each price, and an
`std::unordered_map` from active order IDs to their queue locations.

## Build

From this directory, configure and build with CMake and a C++20 compiler:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Run a synthetic replay

```powershell
.\build\Release\orderbook_demo.exe --events 10000 --seed 42 --workload balanced
```

Supported workloads are `balanced`, `aggressive`, and `cancel-heavy`. Event
count defaults to 10,000, seed defaults to 42, and workload defaults to
`balanced`. The fixed default seed makes feed generation reproducible.

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The lightweight test executable covers adding orders, crossing and partial
fills, price-time priority across price levels, market orders, cancellation,
best-price/quantity updates, and deterministic feed generation.

## Benchmarks

```powershell
.\build\Release\orderbook_benchmark.exe --seed 42 --workload balanced
```

The benchmark replays feeds of 10,000, 100,000, and 1,000,000 events. It
generates each feed before starting `std::chrono::steady_clock` timing and
reports total replay time, events per second, and average nanoseconds per event.
The benchmark accepts the same workload names and seed option as the replay
executable.

### Results

Not measured: this environment does not have a C++ compiler or NMake installed,
so CMake could not configure the project and no benchmark executable could be
built. Run the benchmark command above in a C++20 build environment to record
results. Results depend on the compiler, build configuration, and hardware.

## Synthetic market data

The feed generator uses `std::mt19937_64` seeded from the configurable seed
(default `42`) and emits submissions and cancellations without external data or
dependencies. Balanced feeds mix buy limits, sell limits, and cancellations.
Aggressive feeds include market orders and marketable limit orders alongside
resting limits. Cancel-heavy feeds make cancellations more common. Quantities
and prices are generated from small integer ranges around a fixed reference
price.
