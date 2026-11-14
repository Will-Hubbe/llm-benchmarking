#include "orderbook.hpp"

#include <stdexcept>

namespace orderbook {

std::vector<Trade> OrderBook::submit(const Order& order) {
    if (order.quantity == 0) {
        throw std::invalid_argument("order quantity must be greater than zero");
    }
    if (order.type == OrderType::Limit && order.price <= 0) {
        throw std::invalid_argument("limit order price must be greater than zero");
    }
    if (order_locations_.find(order.id) != order_locations_.end()) {
        throw std::invalid_argument("an active order already uses this ID");
    }

    std::uint64_t remaining = order.quantity;
    std::vector<Trade> trades;

    while (remaining > 0) {
        std::int64_t resting_price = 0;
        OrderQueue* resting_queue = nullptr;

        if (order.side == Side::Buy) {
            if (asks_.empty()) {
                break;
            }
            auto level = asks_.begin();
            if (order.type == OrderType::Limit && level->first > order.price) {
                break;
            }
            resting_price = level->first;
            resting_queue = &level->second;
        } else {
            if (bids_.empty()) {
                break;
            }
            auto level = bids_.begin();
            if (order.type == OrderType::Limit && level->first < order.price) {
                break;
            }
            resting_price = level->first;
            resting_queue = &level->second;
        }

        Order& resting_order = resting_queue->front();
        const std::uint64_t executed = remaining < resting_order.quantity
            ? remaining
            : resting_order.quantity;
        trades.push_back(Trade{
            order.side == Side::Buy ? order.id : resting_order.id,
            order.side == Side::Sell ? order.id : resting_order.id,
            resting_price,
            executed
        });
        remaining -= executed;
        resting_order.quantity -= executed;

        if (resting_order.quantity == 0) {
            const std::uint64_t filled_order_id = resting_order.id;
            order_locations_.erase(filled_order_id);
            resting_queue->pop_front();
            if (resting_queue->empty()) {
                if (order.side == Side::Buy) {
                    asks_.erase(resting_price);
                } else {
                    bids_.erase(resting_price);
                }
            }
        }
    }

    if (remaining > 0 && order.type == OrderType::Limit) {
        Order resting_order = order;
        resting_order.quantity = remaining;

        if (order.side == Side::Buy) {
            auto& queue = bids_[order.price];
            queue.push_back(resting_order);
            order_locations_.emplace(
                order.id,
                OrderLocation{order.side, order.price, std::prev(queue.end())});
        } else {
            auto& queue = asks_[order.price];
            queue.push_back(resting_order);
            order_locations_.emplace(
                order.id,
                OrderLocation{order.side, order.price, std::prev(queue.end())});
        }
    }

    return trades;
}

bool OrderBook::cancel(std::uint64_t order_id) {
    const auto location = order_locations_.find(order_id);
    if (location == order_locations_.end()) {
        return false;
    }

    const Side side = location->second.side;
    const std::int64_t price = location->second.price;
    if (side == Side::Buy) {
        auto level = bids_.find(price);
        level->second.erase(location->second.iterator);
        if (level->second.empty()) {
            bids_.erase(level);
        }
    } else {
        auto level = asks_.find(price);
        level->second.erase(location->second.iterator);
        if (level->second.empty()) {
            asks_.erase(level);
        }
    }
    order_locations_.erase(location);
    return true;
}

std::optional<std::int64_t> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<std::int64_t> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::uint64_t OrderBook::quantity_at(Side side, std::int64_t price) const {
    std::uint64_t total = 0;
    if (side == Side::Buy) {
        const auto level = bids_.find(price);
        if (level != bids_.end()) {
            for (const Order& order : level->second) {
                total += order.quantity;
            }
        }
    } else {
        const auto level = asks_.find(price);
        if (level != asks_.end()) {
            for (const Order& order : level->second) {
                total += order.quantity;
            }
        }
    }
    return total;
}

std::size_t OrderBook::active_order_count() const {
    return order_locations_.size();
}

} // namespace orderbook
