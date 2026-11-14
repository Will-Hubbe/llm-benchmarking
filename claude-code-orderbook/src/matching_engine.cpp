#include "orderbook/matching_engine.hpp"

#include <algorithm>

namespace ob {

namespace {

// Can an incoming limit order on `side` at `limitPrice` trade with a resting
// order priced at `restingPrice`?
inline bool crosses(Side side, Price limitPrice, Price restingPrice) noexcept {
    return side == Side::Buy ? restingPrice <= limitPrice : restingPrice >= limitPrice;
}

}  // namespace

ExecutionReport MatchingEngine::submit(const Order& in, std::vector<Trade>& trades) {
    ExecutionReport report;
    if (in.qty == 0)                { report.reject = RejectReason::ZeroQuantity;     return report; }
    if (in.qty > kMaxOrderQuantity) { report.reject = RejectReason::QuantityTooLarge; return report; }
    if (book_.contains(in.id))      { report.reject = RejectReason::DuplicateOrderId; return report; }

    Order order = in;
    order.seq = nextSeq_++;
    const Side contra = opposite(order.side);
    const bool isLimit = order.type == OrderType::Limit;

    // Walk the contra side from the best price outward until the incoming
    // order is filled, the contra side is empty, or prices stop crossing.
    while (order.qty > 0) {
        const Order* resting = book_.bestOrder(contra);
        if (resting == nullptr) break;
        if (isLimit && !crosses(order.side, order.price, resting->price)) break;

        const Quantity fill = std::min(order.qty, resting->qty);
        if (order.side == Side::Buy)
            trades.push_back(Trade{order.id, resting->id, resting->price, fill, order.side});
        else
            trades.push_back(Trade{resting->id, order.id, resting->price, fill, order.side});

        order.qty -= fill;
        report.filled += fill;
        book_.reduceBestOrder(contra, fill);   // `resting` may dangle after this
    }

    if (order.qty > 0) {
        if (isLimit) {
            book_.addOrder(order);   // cannot fail: id checked unique, qty > 0
            report.rested = order.qty;
        } else {
            report.cancelled = order.qty;
        }
    }
    return report;
}

}  // namespace ob
