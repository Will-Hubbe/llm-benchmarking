// Randomized differential test: the real engine vs. a deliberately naive
// reference implementation (flat vector, linear scan for the best order).
// Same random operations go to both; trades, reports, cancel results and
// full depth must agree after every single operation.
#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace ob;

namespace {

class ReferenceEngine {
public:
    ExecutionReport submit(Order o, std::vector<Trade>& trades) {
        ExecutionReport r;
        if (o.qty == 0) { r.reject = RejectReason::ZeroQuantity; return r; }
        if (o.qty > MatchingEngine::kMaxOrderQuantity) { r.reject = RejectReason::QuantityTooLarge; return r; }
        for (const auto& x : resting_) if (x.id == o.id) { r.reject = RejectReason::DuplicateOrderId; return r; }

        while (o.qty > 0) {
            // Linear scan: best price, then lowest index (= earliest arrival).
            int best = -1;
            for (int i = 0; i < static_cast<int>(resting_.size()); ++i) {
                const Order& x = resting_[i];
                if (x.side == o.side) continue;
                if (o.type == OrderType::Limit &&
                    (o.side == Side::Buy ? x.price > o.price : x.price < o.price)) continue;
                if (best < 0) { best = i; continue; }
                const Order& b = resting_[best];
                if (o.side == Side::Buy ? x.price < b.price : x.price > b.price) best = i;
            }
            if (best < 0) break;
            Order& x = resting_[best];
            Quantity fill = std::min(o.qty, x.qty);
            trades.push_back(o.side == Side::Buy ? Trade{o.id, x.id, x.price, fill, o.side}
                                                 : Trade{x.id, o.id, x.price, fill, o.side});
            o.qty -= fill; x.qty -= fill; r.filled += fill;
            if (x.qty == 0) resting_.erase(resting_.begin() + best);
        }
        if (o.qty > 0) {
            if (o.type == OrderType::Limit) { resting_.push_back(o); r.rested = o.qty; }
            else r.cancelled = o.qty;
        }
        return r;
    }

    bool cancel(OrderId id) {
        for (auto it = resting_.begin(); it != resting_.end(); ++it)
            if (it->id == id) { resting_.erase(it); return true; }
        return false;
    }

    std::vector<DepthLevel> depth(Side side) const {
        std::map<Price, DepthLevel> levels;
        for (const auto& x : resting_) {
            if (x.side != side) continue;
            auto& d = levels[x.price];
            d.price = x.price; d.totalQty += x.qty; ++d.orderCount;
        }
        std::vector<DepthLevel> out;
        for (const auto& [p, d] : levels) out.push_back(d);
        if (side == Side::Buy) std::reverse(out.begin(), out.end());
        return out;
    }

private:
    std::vector<Order> resting_;   // arrival order
};

void runDifferential(std::uint64_t seed, int ops, int priceSpread, int marketPct, int cancelPct) {
    std::mt19937_64 rng(seed);
    auto uni = [&](std::int64_t lo, std::int64_t hi) {
        return std::uniform_int_distribution<std::int64_t>(lo, hi)(rng);
    };

    MatchingEngine eng;
    ReferenceEngine ref;
    std::vector<Trade> te, tr;
    OrderId nextId = 1;
    std::uint64_t totalTrades = 0;

    for (int i = 0; i < ops; ++i) {
        const auto roll = uni(0, 99);
        const std::string where = "seed=" + std::to_string(seed) + " op=" + std::to_string(i);
        if (roll < cancelPct) {
            // Mix of live, stale and never-issued ids.
            const OrderId id = static_cast<OrderId>(uni(1, static_cast<std::int64_t>(nextId) + 5));
            const bool a = eng.cancel(id), b = ref.cancel(id);
            if (a != b) { tl::fail(__FILE__, __LINE__, "cancel mismatch at " + where); throw tl::AbortTest{}; }
        } else {
            Order o;
            // Occasionally reuse a recent id to exercise duplicate rejection.
            o.id = (uni(0, 49) == 0 && nextId > 1) ? nextId - 1 : nextId++;
            o.side = uni(0, 1) ? Side::Buy : Side::Sell;
            o.type = roll < cancelPct + marketPct ? OrderType::Market : OrderType::Limit;
            o.price = 1000 + uni(-priceSpread, priceSpread);
            o.qty = static_cast<Quantity>(uni(0, 1) ? uni(1, 20) : uni(1, 300));
            if (uni(0, 199) == 0) o.qty = 0;   // occasional invalid order
            te.clear(); tr.clear();
            const auto re = eng.submit(o, te), rr = ref.submit(o, tr);
            if (!(re == rr) || te != tr) {
                tl::fail(__FILE__, __LINE__, "submit mismatch at " + where);
                CHECK_EQ(re, rr);
                CHECK_EQ(te, tr);
                throw tl::AbortTest{};
            }
            totalTrades += te.size();
        }
        const auto ebid = eng.book().depth(Side::Buy, SIZE_MAX), rbid = ref.depth(Side::Buy);
        const auto eask = eng.book().depth(Side::Sell, SIZE_MAX), rask = ref.depth(Side::Sell);
        if (ebid != rbid || eask != rask) {
            tl::fail(__FILE__, __LINE__, "depth mismatch at " + where);
            CHECK_EQ(ebid, rbid);
            CHECK_EQ(eask, rask);
            throw tl::AbortTest{};
        }
    }
    CHECK(totalTrades > 0);   // guard against a configuration that never matches
}

}  // namespace

TEST_CASE("reference: tight book, frequent matching (5 seeds x 4000 ops)") {
    for (std::uint64_t s = 1; s <= 5; ++s) runDifferential(s, 4000, 5, 10, 20);
}

TEST_CASE("reference: wide book, many levels (5 seeds x 4000 ops)") {
    for (std::uint64_t s = 100; s <= 104; ++s) runDifferential(s, 4000, 200, 5, 20);
}

TEST_CASE("reference: cancel-heavy flow (5 seeds x 4000 ops)") {
    for (std::uint64_t s = 200; s <= 204; ++s) runDifferential(s, 4000, 20, 5, 60);
}
