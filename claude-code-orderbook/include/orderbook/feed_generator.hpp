#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "orderbook/event.hpp"

namespace ob {

// Parameters for a synthetic order feed. Each event is independently one of:
//   cancel  (probability cancelRatio)  - targets a random previously issued,
//                                        not-yet-cancelled limit order id
//   market  (probability marketRatio)
//   limit   (the rest)                 - aggressive with probability
//                                        aggressiveRatio, otherwise passive
//
// Limit prices (offset drawn uniformly):
//   passive buy  = mid - [1, priceRange]   passive sell = mid + [1, priceRange]
//   aggressive buy = mid + [0, priceRange] aggressive sell = mid - [0, priceRange]
// Passive orders never cross each other; aggressive ones usually cross the spread.
struct FeedConfig {
    std::string   name            = "custom";
    std::uint64_t seed            = 42;
    std::size_t   numEvents       = 100'000;
    double        cancelRatio     = 0.20;
    double        marketRatio     = 0.05;
    double        aggressiveRatio = 0.10;
    Price         midPrice        = 10'000;   // e.g. $100.00 in cents
    Price         priceRange      = 50;
    Quantity      minQty          = 1;
    Quantity      maxQty          = 100;

    // Benchmark workload presets.
    static FeedConfig balanced(std::size_t n, std::uint64_t seed = 42);
    static FeedConfig cancelHeavy(std::size_t n, std::uint64_t seed = 42);
    static FeedConfig aggressive(std::size_t n, std::uint64_t seed = 42);
    static FeedConfig wideBook(std::size_t n, std::uint64_t seed = 42);
};

// Produces exactly cfg.numEvents events. Deterministic for a given config on
// every platform/compiler (no std:: distributions, whose output is
// implementation-defined). Throws std::invalid_argument on a bad config.
std::vector<Event> generateFeed(const FeedConfig& cfg);

}  // namespace ob
