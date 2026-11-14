#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "orderbook/order.hpp"
#include "orderbook/order_book.hpp"
#include "orderbook/trade.hpp"

namespace ob {

enum class RejectReason : std::uint8_t {
    None,
    ZeroQuantity,
    QuantityTooLarge,
    DuplicateOrderId,   // id is already resting in the book
};

// What happened to one submitted order. filled + rested + cancelled == qty
// for accepted orders.
struct ExecutionReport {
    RejectReason reject    = RejectReason::None;
    Quantity     filled    = 0;   // executed against resting orders
    Quantity     rested    = 0;   // limit remainder added to the book
    Quantity     cancelled = 0;   // market remainder discarded (no liquidity left)

    bool accepted() const noexcept { return reject == RejectReason::None; }
    bool operator==(const ExecutionReport&) const = default;
};

// Price-time priority matching engine for a single instrument.
//
// Rules:
//  * Incoming orders match against the opposite side, best price first,
//    earliest arrival first within a price.
//  * Every execution happens at the resting order's price.
//  * A limit order's unfilled remainder rests in the book.
//  * A market order's unfilled remainder is cancelled (immediate-or-cancel).
//  * Order ids must be unique among live (resting) orders.
class MatchingEngine {
public:
    // Cap so a price level's aggregate quantity cannot overflow uint64:
    // that would take > 2^24 (16.7M) max-size orders at a single price.
    static constexpr Quantity kMaxOrderQuantity = Quantity{1} << 40;

    // Processes one order. Trades are *appended* to `trades` (never cleared),
    // so a caller can reuse one pre-reserved buffer and avoid allocation.
    ExecutionReport submit(const Order& order, std::vector<Trade>& trades);

    // Cancels a resting order. Returns false if the id is not live
    // (never existed, already filled, or already cancelled).
    bool cancel(OrderId id) { return book_.cancelOrder(id); }

    const OrderBook& book() const noexcept { return book_; }
    void reserveOrders(std::size_t n) { book_.reserveOrders(n); }

private:
    OrderBook book_;
    SeqNum nextSeq_ = 1;
};

}  // namespace ob
