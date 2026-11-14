#include "orderbook/order_book.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>

namespace ob {

bool OrderBook::addOrder(const Order& order) {
    if (order.qty == 0 || order.type != OrderType::Limit) return false;

    // One hash lookup does both the duplicate check and the insert.
    auto [idx, inserted] = index_.try_emplace(order.id);
    if (!inserted) return false;

    LevelMap& map = levels(order.side);
    auto [lvl, created] = map.try_emplace(order.price);   // O(log P); no-op if level exists
    PriceLevel& level = lvl->second;
    level.orders.push_back(order);
    level.totalQty += order.qty;

    idx->second = Locator{order.side, lvl, std::prev(level.orders.end())};
    return true;
}

bool OrderBook::cancelOrder(OrderId id) {
    auto idx = index_.find(id);
    if (idx == index_.end()) return false;

    const Locator& loc = idx->second;
    PriceLevel& level = loc.level->second;
    level.totalQty -= loc.order->qty;
    level.orders.erase(loc.order);                         // O(1)
    if (level.orders.empty()) levels(loc.side).erase(loc.level);   // O(1) amortized via iterator

    index_.erase(idx);
    return true;
}

const Order* OrderBook::bestOrder(Side side) const {
    const LevelMap& map = levels(side);
    if (map.empty()) return nullptr;
    return &map.begin()->second.orders.front();
}

void OrderBook::reduceBestOrder(Side side, Quantity qty) {
    LevelMap& map = levels(side);
    assert(!map.empty());
    auto lvl = map.begin();
    PriceLevel& level = lvl->second;
    Order& front = level.orders.front();
    assert(qty > 0 && qty <= front.qty);

    front.qty -= qty;
    level.totalQty -= qty;
    if (front.qty == 0) {
        index_.erase(front.id);
        level.orders.pop_front();
        if (level.orders.empty()) map.erase(lvl);
    }
}

std::optional<Price> OrderBook::bestPrice(Side side) const {
    const LevelMap& map = levels(side);
    if (map.empty()) return std::nullopt;
    return map.begin()->first;
}

std::vector<DepthLevel> OrderBook::depth(Side side, std::size_t maxLevels) const {
    const LevelMap& map = levels(side);
    std::vector<DepthLevel> out;
    out.reserve(std::min(maxLevels, map.size()));
    for (const auto& [price, level] : map) {
        if (out.size() >= maxLevels) break;
        out.push_back({price, level.totalQty, level.orders.size()});
    }
    return out;
}

const Order* OrderBook::findOrder(OrderId id) const {
    auto idx = index_.find(id);
    return idx == index_.end() ? nullptr : &*idx->second.order;
}

}  // namespace ob
