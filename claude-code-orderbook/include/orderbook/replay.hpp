#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "orderbook/event.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/trade.hpp"

namespace ob {

// Summary of a replay. `checksum` is an order-sensitive hash over every trade:
// two runs that produce the same trades in the same order have equal checksums,
// which lets later optimizations prove they didn't change engine behavior.
struct ReplayStats {
    std::size_t   ordersSubmitted  = 0;
    std::size_t   ordersRejected   = 0;
    std::size_t   cancelsRequested = 0;
    std::size_t   cancelsSucceeded = 0;
    std::size_t   trades           = 0;
    Quantity      volume           = 0;   // total traded quantity
    std::uint64_t checksum         = 0;

    bool operator==(const ReplayStats&) const = default;
};

namespace detail {
inline std::uint64_t mix(std::uint64_t h, std::uint64_t v) noexcept {
    // splitmix64 finalizer over (h + v); cheap, good avalanche.
    std::uint64_t x = h + v + 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
}  // namespace detail

// Applies one event. Trades produced are appended to `trades`; only the newly
// appended ones are folded into `stats`. Inline so the benchmark's timed loop
// pays no cross-TU call for it.
inline void applyEvent(MatchingEngine& engine, const Event& ev, std::vector<Trade>& trades,
                       ReplayStats& stats) {
    if (ev.type == EventType::Cancel) {
        ++stats.cancelsRequested;
        if (engine.cancel(ev.order.id)) ++stats.cancelsSucceeded;
        return;
    }
    ++stats.ordersSubmitted;
    const std::size_t first = trades.size();
    if (!engine.submit(ev.order, trades).accepted()) ++stats.ordersRejected;
    for (std::size_t i = first; i < trades.size(); ++i) {
        const Trade& t = trades[i];
        stats.volume += t.qty;
        std::uint64_t h = detail::mix(stats.checksum, t.buyOrderId);
        h = detail::mix(h, t.sellOrderId);
        h = detail::mix(h, static_cast<std::uint64_t>(t.price));
        stats.checksum = detail::mix(h, t.qty);
    }
    stats.trades += trades.size() - first;
}

// Replays every event in order. If `allTrades` is given, every trade is
// appended to it; otherwise a small internal scratch buffer is reused.
ReplayStats replay(MatchingEngine& engine, std::span<const Event> events,
                   std::vector<Trade>* allTrades = nullptr);

// ---- CSV persistence (never used inside timed code) -------------------------
// Events format:
//   type,id,side,order_type,price,qty
//   add,1,buy,limit,9990,25
//   add,2,sell,market,0,10
//   cancel,1,,,,
void saveEventsCsv(const std::filesystem::path& path, std::span<const Event> events);
// Throws std::runtime_error (with the line number) on malformed input.
std::vector<Event> loadEventsCsv(const std::filesystem::path& path);
//   buy_id,sell_id,price,qty,aggressor
void saveTradesCsv(const std::filesystem::path& path, std::span<const Trade> trades);

}  // namespace ob
