#pragma once

#include "orderbook/types.hpp"

namespace ob {

// One order. While resting in the book, `qty` is the *remaining* quantity and
// is decremented in place as the order is partially filled.
struct Order {
    OrderId   id    = 0;
    Side      side  = Side::Buy;
    OrderType type  = OrderType::Limit;
    Price     price = 0;   // ignored for market orders
    Quantity  qty   = 0;
    SeqNum    seq   = 0;   // assigned by the matching engine on arrival

    bool operator==(const Order&) const = default;
};

}  // namespace ob
