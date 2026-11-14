#pragma once

#include "orderbook/types.hpp"

namespace ob {

// One execution between an incoming (aggressor) order and a resting order.
// Executions always happen at the resting order's price.
struct Trade {
    OrderId  buyOrderId  = 0;
    OrderId  sellOrderId = 0;
    Price    price       = 0;
    Quantity qty         = 0;
    Side     aggressor   = Side::Buy;   // side of the incoming order

    bool operator==(const Trade&) const = default;
};

}  // namespace ob
