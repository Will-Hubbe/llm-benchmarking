#pragma once
// Printing + construction helpers shared by the test files.
#include <ostream>

#include "orderbook/matching_engine.hpp"
#include "orderbook/order.hpp"
#include "orderbook/order_book.hpp"
#include "orderbook/trade.hpp"

namespace ob {

inline std::ostream& operator<<(std::ostream& os, Side s) {
    return os << (s == Side::Buy ? "Buy" : "Sell");
}
inline std::ostream& operator<<(std::ostream& os, const Order& o) {
    return os << "Order{id=" << o.id << ' ' << o.side << ' '
              << (o.type == OrderType::Limit ? "Limit" : "Market")
              << " px=" << o.price << " qty=" << o.qty << " seq=" << o.seq << '}';
}
inline std::ostream& operator<<(std::ostream& os, const Trade& t) {
    return os << "Trade{buy=" << t.buyOrderId << " sell=" << t.sellOrderId << " px=" << t.price
              << " qty=" << t.qty << " aggr=" << t.aggressor << '}';
}
inline std::ostream& operator<<(std::ostream& os, const DepthLevel& d) {
    return os << "Level{px=" << d.price << " qty=" << d.totalQty << " n=" << d.orderCount << '}';
}
inline std::ostream& operator<<(std::ostream& os, RejectReason r) {
    switch (r) {
        case RejectReason::None:             return os << "None";
        case RejectReason::ZeroQuantity:     return os << "ZeroQuantity";
        case RejectReason::QuantityTooLarge: return os << "QuantityTooLarge";
        case RejectReason::DuplicateOrderId: return os << "DuplicateOrderId";
    }
    return os << "?";
}
inline std::ostream& operator<<(std::ostream& os, const ExecutionReport& r) {
    return os << "Report{reject=" << r.reject << " filled=" << r.filled << " rested=" << r.rested
              << " cancelled=" << r.cancelled << '}';
}

}  // namespace ob

namespace testutil {

inline ob::Order limit(ob::OrderId id, ob::Side side, ob::Price price, ob::Quantity qty) {
    return ob::Order{id, side, ob::OrderType::Limit, price, qty, 0};
}
inline ob::Order market(ob::OrderId id, ob::Side side, ob::Quantity qty) {
    return ob::Order{id, side, ob::OrderType::Market, 0, qty, 0};
}

}  // namespace testutil
