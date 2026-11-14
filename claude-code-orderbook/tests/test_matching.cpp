// Matching engine: limit/market orders, partial & complete fills, price-time priority.
// Every test checks the exact trades produced AND the resulting book.
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
}  // namespace

// ---- Insertion without matching ---------------------------------------------

TEST_CASE("match: limit buy into empty book rests, no trades") {
    MatchingEngine eng;
    Trades t;
    CHECK_EQ(eng.submit(limit(1, Side::Buy, 100, 10), t), rep(0, 10, 0));
    CHECK(t.empty());
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{100, 10, 1}}));
    CHECK(eng.book().depth(Side::Sell, 5).empty());
}

TEST_CASE("match: non-crossing buy and sell both rest (spread stays open)") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 99, 10), t);
    eng.submit(limit(2, Side::Sell, 101, 10), t);
    eng.submit(limit(3, Side::Sell, 100, 5), t);   // still above the bid
    CHECK(t.empty());
    CHECK_EQ(eng.book().bestBid(), std::optional<Price>(99));
    CHECK_EQ(eng.book().bestAsk(), std::optional<Price>(100));
    CHECK_EQ(eng.book().orderCount(), 3);
}

TEST_CASE("match: engine assigns increasing sequence numbers") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 1), t);
    eng.submit(limit(2, Side::Buy, 100, 1), t);
    REQUIRE(eng.book().findOrder(1) && eng.book().findOrder(2));
    CHECK(eng.book().findOrder(1)->seq < eng.book().findOrder(2)->seq);
}

// ---- Matching at identical prices / complete fills ---------------------------

TEST_CASE("match: buy aggressor fully matches resting sell at same price") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 10), t);
    CHECK_EQ(eng.submit(limit(2, Side::Buy, 100, 10), t), rep(10, 0, 0));
    CHECK_EQ(t, (Trades{{2, 1, 100, 10, Side::Buy}}));
    CHECK(eng.book().empty());
    CHECK(!eng.book().bestBid() && !eng.book().bestAsk());
}

TEST_CASE("match: sell aggressor fully matches resting buy at same price") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 10), t);
    CHECK_EQ(eng.submit(limit(2, Side::Sell, 100, 10), t), rep(10, 0, 0));
    CHECK_EQ(t, (Trades{{1, 2, 100, 10, Side::Sell}}));   // buy id first, always
    CHECK(eng.book().empty());
}

TEST_CASE("match: execution happens at the resting order's price") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 5), t);
    eng.submit(limit(2, Side::Buy, 105, 5), t);   // willing to pay more
    eng.submit(limit(3, Side::Buy, 90, 5), t);
    eng.submit(limit(4, Side::Sell, 80, 5), t);   // willing to sell lower
    CHECK_EQ(t, (Trades{{2, 1, 100, 5, Side::Buy}, {3, 4, 90, 5, Side::Sell}}));
    CHECK(eng.book().empty());
}

TEST_CASE("match: trades are appended to the caller's buffer, not cleared") {
    MatchingEngine eng;
    Trades t{{99, 98, 1, 1, Side::Buy}};
    eng.submit(limit(1, Side::Sell, 100, 1), t);
    eng.submit(limit(2, Side::Buy, 100, 1), t);
    CHECK_EQ(t, (Trades{{99, 98, 1, 1, Side::Buy}, {2, 1, 100, 1, Side::Buy}}));
}

// ---- Partial fills ----------------------------------------------------------

TEST_CASE("match: aggressor partially filled, remainder rests at its limit") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 4), t);
    CHECK_EQ(eng.submit(limit(2, Side::Buy, 101, 10), t), rep(4, 6, 0));
    CHECK_EQ(t, (Trades{{2, 1, 100, 4, Side::Buy}}));
    CHECK(!eng.book().bestAsk());
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{101, 6, 1}}));
    REQUIRE(eng.book().findOrder(2) != nullptr);
    CHECK_EQ(eng.book().findOrder(2)->qty, 6);
}

TEST_CASE("match: resting order partially filled keeps reduced qty") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 10), t);
    CHECK_EQ(eng.submit(limit(2, Side::Sell, 100, 3), t), rep(3, 0, 0));
    CHECK_EQ(t, (Trades{{1, 2, 100, 3, Side::Sell}}));
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{100, 7, 1}}));
    CHECK_EQ(eng.book().findOrder(1)->qty, 7);
    CHECK(!eng.book().contains(2));
}

TEST_CASE("match: partially filled order keeps its place in the queue") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 10), t);
    eng.submit(limit(2, Side::Sell, 100, 10), t);
    eng.submit(limit(3, Side::Buy, 100, 4), t);    // partially fills #1
    eng.submit(limit(4, Side::Buy, 100, 8), t);    // must finish #1 before touching #2
    CHECK_EQ(t, (Trades{{3, 1, 100, 4, Side::Buy},
                        {4, 1, 100, 6, Side::Buy},
                        {4, 2, 100, 2, Side::Buy}}));
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{100, 8, 1}}));
}

TEST_CASE("match: one aggressor fills several resting orders completely") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 50, 1), t);
    eng.submit(limit(2, Side::Buy, 50, 2), t);
    eng.submit(limit(3, Side::Buy, 50, 3), t);
    CHECK_EQ(eng.submit(limit(4, Side::Sell, 50, 6), t), rep(6, 0, 0));
    CHECK_EQ(t, (Trades{{1, 4, 50, 1, Side::Sell}, {2, 4, 50, 2, Side::Sell}, {3, 4, 50, 3, Side::Sell}}));
    CHECK(eng.book().empty());
}

// ---- Price-time priority ----------------------------------------------------

TEST_CASE("match: price priority - best ask executes first regardless of arrival") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 103, 5), t);
    eng.submit(limit(2, Side::Sell, 101, 5), t);   // arrives later, better price
    eng.submit(limit(3, Side::Sell, 102, 5), t);
    eng.submit(limit(4, Side::Buy, 103, 7), t);
    CHECK_EQ(t, (Trades{{4, 2, 101, 5, Side::Buy}, {4, 3, 102, 2, Side::Buy}}));
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{102, 3, 1}, {103, 5, 1}}));
}

TEST_CASE("match: price priority - best bid executes first") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 97, 5), t);
    eng.submit(limit(2, Side::Buy, 99, 5), t);
    eng.submit(limit(3, Side::Buy, 98, 5), t);
    eng.submit(limit(4, Side::Sell, 97, 12), t);
    CHECK_EQ(t, (Trades{{2, 4, 99, 5, Side::Sell}, {3, 4, 98, 5, Side::Sell}, {1, 4, 97, 2, Side::Sell}}));
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{97, 3, 1}}));
}

TEST_CASE("match: time priority - earlier order at same price fills first") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);
    eng.submit(limit(2, Side::Buy, 100, 50), t);   // larger, but later
    eng.submit(limit(3, Side::Buy, 100, 5), t);
    eng.submit(limit(4, Side::Sell, 100, 7), t);
    CHECK_EQ(t, (Trades{{1, 4, 100, 5, Side::Sell}, {2, 4, 100, 2, Side::Sell}}));
    CHECK_EQ(eng.book().findOrder(2)->qty, 48);
    CHECK_EQ(eng.book().findOrder(3)->qty, 5);
}

TEST_CASE("match: limit price stops the sweep; remainder rests") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 5), t);
    eng.submit(limit(2, Side::Sell, 101, 5), t);
    eng.submit(limit(3, Side::Sell, 102, 5), t);
    CHECK_EQ(eng.submit(limit(4, Side::Buy, 101, 20), t), rep(10, 10, 0));
    CHECK_EQ(t, (Trades{{4, 1, 100, 5, Side::Buy}, {4, 2, 101, 5, Side::Buy}}));
    CHECK_EQ(eng.book().bestBid(), std::optional<Price>(101));
    CHECK_EQ(eng.book().bestAsk(), std::optional<Price>(102));
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{102, 5, 1}}));
}

// ---- Multiple price levels --------------------------------------------------

TEST_CASE("match: book with several levels per side reports correct depth") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 98, 10), t);
    eng.submit(limit(2, Side::Buy, 99, 20), t);
    eng.submit(limit(3, Side::Buy, 99, 5), t);
    eng.submit(limit(4, Side::Sell, 101, 7), t);
    eng.submit(limit(5, Side::Sell, 102, 8), t);
    eng.submit(limit(6, Side::Sell, 101, 3), t);
    CHECK(t.empty());
    CHECK_EQ(eng.book().depth(Side::Buy, 10), (Depth{{99, 25, 2}, {98, 10, 1}}));
    CHECK_EQ(eng.book().depth(Side::Sell, 10), (Depth{{101, 10, 2}, {102, 8, 1}}));
}

TEST_CASE("match: aggressive limit sweeps all levels and becomes new best") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 1), t);
    eng.submit(limit(2, Side::Buy, 99, 2), t);
    eng.submit(limit(3, Side::Buy, 98, 3), t);
    CHECK_EQ(eng.submit(limit(4, Side::Sell, 90, 10), t), rep(6, 4, 0));
    CHECK_EQ(t, (Trades{{1, 4, 100, 1, Side::Sell}, {2, 4, 99, 2, Side::Sell}, {3, 4, 98, 3, Side::Sell}}));
    CHECK(!eng.book().bestBid());
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{90, 4, 1}}));
}

// ---- Market orders ----------------------------------------------------------

TEST_CASE("match: market buy sweeps asks with no price limit") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 5), t);
    eng.submit(limit(2, Side::Sell, 150, 5), t);
    eng.submit(limit(3, Side::Sell, 999, 5), t);
    CHECK_EQ(eng.submit(market(4, Side::Buy, 12), t), rep(12, 0, 0));
    CHECK_EQ(t, (Trades{{4, 1, 100, 5, Side::Buy}, {4, 2, 150, 5, Side::Buy}, {4, 3, 999, 2, Side::Buy}}));
    CHECK_EQ(eng.book().depth(Side::Sell, 5), (Depth{{999, 3, 1}}));
    CHECK(!eng.book().bestBid());
}

TEST_CASE("match: market sell sweeps bids with no price limit") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);
    eng.submit(limit(2, Side::Buy, 1, 5), t);
    CHECK_EQ(eng.submit(market(3, Side::Sell, 7), t), rep(7, 0, 0));
    CHECK_EQ(t, (Trades{{1, 3, 100, 5, Side::Sell}, {2, 3, 1, 2, Side::Sell}}));
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{1, 3, 1}}));
}

TEST_CASE("match: market order larger than book - remainder cancelled, never rests") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 100, 3), t);
    CHECK_EQ(eng.submit(market(2, Side::Buy, 10), t), rep(3, 0, 7));
    CHECK_EQ(t, (Trades{{2, 1, 100, 3, Side::Buy}}));
    CHECK(eng.book().empty());
    CHECK(!eng.book().contains(2));
}

TEST_CASE("match: market order into empty book - nothing happens") {
    MatchingEngine eng;
    Trades t;
    CHECK_EQ(eng.submit(market(1, Side::Buy, 10), t), rep(0, 0, 10));
    CHECK_EQ(eng.submit(market(2, Side::Sell, 10), t), rep(0, 0, 10));
    CHECK(t.empty());
    CHECK(eng.book().empty());
}

TEST_CASE("match: market order ignores its own price field") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Sell, 500, 2), t);
    Order m = market(2, Side::Buy, 2);
    m.price = 1;   // would not cross if treated as a limit
    CHECK_EQ(eng.submit(m, t), rep(2, 0, 0));
    CHECK_EQ(t, (Trades{{2, 1, 500, 2, Side::Buy}}));
}

TEST_CASE("match: market order only takes from the opposite side") {
    MatchingEngine eng;
    Trades t;
    eng.submit(limit(1, Side::Buy, 100, 5), t);      // same side as aggressor
    CHECK_EQ(eng.submit(market(2, Side::Buy, 5), t), rep(0, 0, 5));
    CHECK(t.empty());
    CHECK_EQ(eng.book().depth(Side::Buy, 5), (Depth{{100, 5, 1}}));
}
