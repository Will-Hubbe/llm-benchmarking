#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "orderbook/order.hpp"

namespace ob {

// All resting orders at one price. std::list gives FIFO time priority
// (front = oldest) and O(1) erase from the middle via a stored iterator.
struct PriceLevel {
    std::list<Order> orders;
    Quantity totalQty = 0;   // maintained incrementally -> depth queries are cheap
};

// One row of a market-depth snapshot.
struct DepthLevel {
    Price       price      = 0;
    Quantity    totalQty   = 0;
    std::size_t orderCount = 0;

    bool operator==(const DepthLevel&) const = default;
};

// The order book only *stores* resting limit orders; it has no matching logic.
// The MatchingEngine drives it through bestOrder() / reduceBestOrder().
class OrderBook {
private:
    // One comparator type for both sides so bids and asks share a map type
    // (and therefore an iterator type, which the order index stores).
    // begin() is always the best price: highest bid / lowest ask.
    struct PriceCompare {
        bool descending = false;
        bool operator()(Price a, Price b) const noexcept { return descending ? a > b : a < b; }
    };
    using LevelMap = std::map<Price, PriceLevel, PriceCompare>;

    // Where a live order lives, so cancel never has to search.
    struct Locator {
        Side side = Side::Buy;
        LevelMap::iterator level;
        std::list<Order>::iterator order;
    };

public:
    // Adds a resting limit order at the back of its price level.
    // Returns false (book unchanged) if the id is already live, qty == 0,
    // or the order is not a limit order.
    bool addOrder(const Order& order);

    // Removes a live order. Returns false if the id is unknown.
    bool cancelOrder(OrderId id);

    // Highest-priority resting order on `side` (best price, earliest arrival),
    // or nullptr if that side is empty.
    const Order* bestOrder(Side side) const;

    // Fills `qty` of bestOrder(side). Precondition: side non-empty and
    // 0 < qty <= bestOrder(side)->qty. Removes the order (and level) when it
    // reaches zero.
    void reduceBestOrder(Side side, Quantity qty);

    std::optional<Price> bestPrice(Side side) const;
    std::optional<Price> bestBid() const { return bestPrice(Side::Buy); }
    std::optional<Price> bestAsk() const { return bestPrice(Side::Sell); }

    // Up to `maxLevels` levels from the best price outward.
    std::vector<DepthLevel> depth(Side side, std::size_t maxLevels) const;

    const Order* findOrder(OrderId id) const;
    bool contains(OrderId id) const { return index_.contains(id); }

    std::size_t orderCount() const noexcept { return index_.size(); }
    std::size_t levelCount(Side side) const noexcept { return levels(side).size(); }
    bool empty() const noexcept { return index_.empty(); }

    // Pre-size the id index to avoid rehashing during a run.
    void reserveOrders(std::size_t n) { index_.reserve(n); }

private:
    LevelMap& levels(Side s) noexcept { return s == Side::Buy ? bids_ : asks_; }
    const LevelMap& levels(Side s) const noexcept { return s == Side::Buy ? bids_ : asks_; }

    LevelMap bids_{PriceCompare{true}};
    LevelMap asks_{PriceCompare{false}};
    std::unordered_map<OrderId, Locator> index_;
};

}  // namespace ob
