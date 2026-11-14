// Cancellation through the engine, input validation, and edge cases.
#include <limits>
#include <vector>

#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace ob;
using testutil::limit;
using testutil::market;
using Trades = std::vector<Trade>;
using Depth = std::vector<DepthLevel>;

namespace {
ExecutionReport rep(Quantity filled, Quantity rested, Quantity cancelled) {
    return ExecutionReport{RejectReason::None, filled, rested, cancelled};
}
ExecutionReport rejected(RejectReason r) { return ExecutionReport{r, 0, 0, 0}; }
}  // namespace

// ---- Cancellation -----------------------------------------------------------

TEST_CASE("cancel: existing order is removed and can no longer trade") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 5), t);
    eng.submit(limit(2, Side::Sell, 101, 5), t);
    CHECK(eng.cancel(1));
    CHECK(!eng.book().contains(1));
    CHECK_EQ(eng.book().bestAsk(), std::optional<Price>(101));

    eng.submit(limit(3, Side::Buy, 101, 5), t);
    CHECK_EQ(t, (Trades{{3, 2, 101, 5, Side::Buy}}));   // skipped the cancelled order
    CHECK(eng.book().empty());
}

TEST_CASE("cancel: front of queue cancelled - next order inherits priority") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);
    eng.submit(limit(2, Side::Buy, 100, 5), t);
    eng.submit(limit(3, Side::Buy, 100, 5), t);
    CHECK(eng.cancel(1));
    eng.submit(limit(4, Side::Sell, 100, 7), t);
    CHECK_EQ(t, (Trades{{2, 4, 100, 5, Side::Sell}, {3, 4, 100, 2, Side::Sell}}));
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{100, 3, 1}}));
}

TEST_CASE("cancel: partially filled order removes only its remainder") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 10), t);
    eng.submit(limit(2, Side::Sell, 100, 10), t);
    eng.submit(limit(3, Side::Buy, 100, 4), t);
    CHECK(eng.cancel(1));
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{100, 10, 1}}));
    CHECK_EQ(t, (Trades{{3, 1, 100, 4, Side::Buy}}));   // past trades unaffected
}

TEST_CASE("cancel: nonexistent id returns false and leaves book unchanged") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);
    CHECK(!eng.cancel(999));
    CHECK(!eng.cancel(0));
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{100, 5, 1}}));
}

TEST_CASE("cancel: fully filled order can no longer be cancelled") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 5), t);
    eng.submit(limit(2, Side::Buy, 100, 5), t);
    CHECK(!eng.cancel(1));   // filled as resting order
    CHECK(!eng.cancel(2));   // filled as aggressor, never rested
}

TEST_CASE("cancel: market order id is never cancellable (never rests)") {
    MatchingEngine eng;
    Trades t;
    eng.submit(market(1, Side::Buy, 5), t);
    CHECK(!eng.cancel(1));
}

TEST_CASE("cancel: double cancel - second returns false") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);
    CHECK(eng.cancel(1));
    CHECK(!eng.cancel(1));
    CHECK(eng.book().empty());
}

TEST_CASE("cancel: on empty book returns false") {
    MatchingEngine eng;
    CHECK(!eng.cancel(1));
    CHECK(eng.book().empty());
}

// ---- Validation -------------------------------------------------------------

TEST_CASE("edge: zero quantity rejected for limit and market orders") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 5), t);
    CHECK_EQ(eng.submit(limit(2, Side::Buy, 100, 0), t), rejected(RejectReason::ZeroQuantity));
    CHECK_EQ(eng.submit(market(3, Side::Buy, 0), t), rejected(RejectReason::ZeroQuantity));
    CHECK(t.empty());
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{100, 5, 1}}));
}

TEST_CASE("edge: quantity above the cap rejected; exactly the cap accepted") {
    MatchingEngine eng;
    Trades t;
    constexpr Quantity kMax = MatchingEngine::kMaxOrderQuantity;
    CHECK_EQ(eng.submit(limit(1, Side::Buy, 100, kMax + 1), t), rejected(RejectReason::QuantityTooLarge));
    CHECK_EQ(eng.submit(limit(2, Side::Buy, 100, std::numeric_limits<Quantity>::max()), t),
             rejected(RejectReason::QuantityTooLarge));
    CHECK(eng.book().empty());
    CHECK_EQ(eng.submit(limit(3, Side::Buy, 100, kMax), t), rep(0, kMax, 0));
    CHECK_EQ(eng.book().depth(Side::Buy, 1), (Depth{{100, kMax, 1}}));
}

TEST_CASE("edge: duplicate live id rejected for limit and market; book unchanged") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);
    eng.submit(limit(2, Side::Sell, 105, 5), t);
    CHECK_EQ(eng.submit(limit(1, Side::Sell, 100, 5), t), rejected(RejectReason::DuplicateOrderId));
    CHECK_EQ(eng.submit(market(2, Side::Buy, 5), t), rejected(RejectReason::DuplicateOrderId));
    CHECK(t.empty());
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{100, 5, 1}}));
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{105, 5, 1}}));
}

TEST_CASE("edge: id may be reused once the original is filled or cancelled") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);
    eng.submit(limit(2, Side::Sell, 100, 5), t);   // fills #1
    CHECK_EQ(eng.submit(limit(1, Side::Sell, 110, 3), t), rep(0, 3, 0));
    CHECK(eng.cancel(1));
    CHECK_EQ(eng.submit(limit(1, Side::Buy, 90, 3), t), rep(0, 3, 0));
}

// ---- Large quantities and extreme prices ------------------------------------

TEST_CASE("edge: max-size order against many small resting orders") {
    MatchingEngine eng;
    Trades t;
    constexpr Quantity kMax = MatchingEngine::kMaxOrderQuantity;
    for (OrderId i = 1; i <= 100; ++i) eng.submit(limit(i, Side::Sell, 100 + static_cast<Price>(i % 5), 1000), t);
    CHECK_EQ(eng.submit(limit(1000, Side::Buy, 200, kMax), t), rep(100'000, kMax - 100'000, 0));
    CHECK_EQ(t.size(), 100);
    // Price priority across levels: all 100-tick fills come first, then 101, ...
    for (std::size_t i = 1; i < t.size(); ++i) CHECK(t[i - 1].price <= t[i].price);
    CHECK(!eng.book().bestAsk());
    CHECK_EQ(eng.book().depth(Side::Buy, 1), (Depth{{200, kMax - 100'000, 1}}));
}

TEST_CASE("edge: many max-size orders at one level aggregate without overflow") {
    MatchingEngine eng;
    Trades t;
    constexpr Quantity kMax = MatchingEngine::kMaxOrderQuantity;
    for (OrderId i = 1; i <= 1000; ++i) eng.submit(limit(i, Side::Sell, 100, kMax), t);
    CHECK_EQ(eng.book().depth(Side::Sell, 1), (Depth{{100, kMax * 1000, 1000}}));
    CHECK_EQ(eng.submit(market(5000, Side::Buy, kMax), t), rep(kMax, 0, 0));
    CHECK_EQ(eng.book().depth(Side::Sell, 1), (Depth{{100, kMax * 999, 999}}));
}

TEST_CASE("edge: extreme limit prices cross everything on the other side") {
    MatchingEngine eng;
    Trades t;
    constexpr Price kHigh = std::numeric_limits<Price>::max();
    constexpr Price kLow = std::numeric_limits<Price>::min();
    eng.submit(limit(1, Side::Sell, kHigh, 1), t);
    eng.submit(limit(2, Side::Buy, kHigh, 1), t);   // crosses at kHigh
    eng.submit(limit(3, Side::Buy, kLow, 1), t);
    eng.submit(limit(4, Side::Sell, kLow, 1), t);   // crosses at kLow
    CHECK_EQ(t, (Trades{{2, 1, kHigh, 1, Side::Buy}, {3, 4, kLow, 1, Side::Sell}}));
    CHECK(eng.book().empty());
}

TEST_CASE("edge: negative and zero prices match normally") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, -10, 2), t);
    eng.submit(limit(2, Side::Sell, 0, 2), t);
    eng.submit(limit(3, Side::Buy, 0, 3), t);
    CHECK_EQ(t, (Trades{{3, 1, -10, 2, Side::Buy}, {3, 2, 0, 1, Side::Buy}}));
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{0, 1, 1}}));
}

TEST_CASE("edge: book drained to empty then rebuilt behaves like new") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);
    eng.submit(market(2, Side::Sell, 5), t);
    CHECK(eng.book().empty());
    CHECK(!eng.book().bestBid() && !eng.book().bestAsk());
    CHECK(eng.book().depth(Side::Buy, 5).empty());
    t.clear();
    eng.submit(limit(3, Side::Sell, 50, 1), t);
    eng.submit(limit(4, Side::Buy, 50, 1), t);
    CHECK_EQ(t, (Trades{{4, 3, 50, 1, Side::Buy}}));
}
