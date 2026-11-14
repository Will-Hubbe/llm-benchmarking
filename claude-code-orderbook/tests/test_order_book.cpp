// Tests for the passive OrderBook container (no matching here).
#include <limits>
#include <vector>

#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace ob;
using testutil::limit;
using testutil::market;

TEST_CASE("book: empty book has no best prices, no depth, nothing to cancel") {
    OrderBook book;
    CHECK(book.empty());
    CHECK_EQ(book.orderCount(), 0);
    CHECK(!book.bestBid().has_value());
    CHECK(!book.bestAsk().has_value());
    CHECK(book.bestOrder(Side::Buy) == nullptr);
    CHECK(book.bestOrder(Side::Sell) == nullptr);
    CHECK(book.depth(Side::Buy, 10).empty());
    CHECK(book.depth(Side::Sell, 10).empty());
    CHECK(!book.cancelOrder(42));
    CHECK(book.findOrder(42) == nullptr);
}

TEST_CASE("book: single buy and single sell set best bid / best ask") {
    OrderBook book;
    REQUIRE(book.addOrder(limit(1, Side::Buy, 100, 10)));
    REQUIRE(book.addOrder(limit(2, Side::Sell, 105, 7)));

    CHECK_EQ(book.bestBid(), std::optional<Price>(100));
    CHECK_EQ(book.bestAsk(), std::optional<Price>(105));
    CHECK_EQ(book.orderCount(), 2);
    CHECK_EQ(book.depth(Side::Buy, 5), (std::vector<DepthLevel>{{100, 10, 1}}));
    CHECK_EQ(book.depth(Side::Sell, 5), (std::vector<DepthLevel>{{105, 7, 1}}));
}

TEST_CASE("book: multiple levels sorted best-first (bids desc, asks asc)") {
    OrderBook book;
    book.addOrder(limit(1, Side::Buy, 99, 1));
    book.addOrder(limit(2, Side::Buy, 101, 2));
    book.addOrder(limit(3, Side::Buy, 100, 3));
    book.addOrder(limit(4, Side::Sell, 106, 4));
    book.addOrder(limit(5, Side::Sell, 104, 5));
    book.addOrder(limit(6, Side::Sell, 105, 6));

    CHECK_EQ(book.bestBid(), std::optional<Price>(101));
    CHECK_EQ(book.bestAsk(), std::optional<Price>(104));
    CHECK_EQ(book.levelCount(Side::Buy), 3);
    CHECK_EQ(book.levelCount(Side::Sell), 3);
    CHECK_EQ(book.depth(Side::Buy, 10),
             (std::vector<DepthLevel>{{101, 2, 1}, {100, 3, 1}, {99, 1, 1}}));
    CHECK_EQ(book.depth(Side::Sell, 10),
             (std::vector<DepthLevel>{{104, 5, 1}, {105, 6, 1}, {106, 4, 1}}));
}

TEST_CASE("book: depth is truncated to maxLevels") {
    OrderBook book;
    for (OrderId i = 1; i <= 5; ++i) book.addOrder(limit(i, Side::Buy, 100 - static_cast<Price>(i), 1));
    CHECK_EQ(book.depth(Side::Buy, 2), (std::vector<DepthLevel>{{99, 1, 1}, {98, 1, 1}}));
    CHECK(book.depth(Side::Buy, 0).empty());
}

TEST_CASE("book: orders at the same price aggregate into one level") {
    OrderBook book;
    book.addOrder(limit(1, Side::Sell, 200, 10));
    book.addOrder(limit(2, Side::Sell, 200, 20));
    book.addOrder(limit(3, Side::Sell, 200, 30));
    CHECK_EQ(book.levelCount(Side::Sell), 1);
    CHECK_EQ(book.depth(Side::Sell, 1), (std::vector<DepthLevel>{{200, 60, 3}}));
}

TEST_CASE("book: bestOrder respects time priority within a level") {
    OrderBook book;
    book.addOrder(limit(10, Side::Buy, 100, 1));
    book.addOrder(limit(11, Side::Buy, 100, 2));
    book.addOrder(limit(12, Side::Buy, 99, 3));   // worse price, irrelevant

    REQUIRE(book.bestOrder(Side::Buy) != nullptr);
    CHECK_EQ(book.bestOrder(Side::Buy)->id, 10);
    book.cancelOrder(10);
    CHECK_EQ(book.bestOrder(Side::Buy)->id, 11);
    book.cancelOrder(11);
    CHECK_EQ(book.bestOrder(Side::Buy)->id, 12);
}

TEST_CASE("book: duplicate id is rejected and the book is unchanged") {
    OrderBook book;
    REQUIRE(book.addOrder(limit(1, Side::Buy, 100, 10)));
    CHECK(!book.addOrder(limit(1, Side::Sell, 120, 5)));   // same id, other side
    CHECK(!book.addOrder(limit(1, Side::Buy, 100, 5)));    // same id, same level
    CHECK_EQ(book.orderCount(), 1);
    CHECK(!book.bestAsk().has_value());
    CHECK_EQ(book.depth(Side::Buy, 5), (std::vector<DepthLevel>{{100, 10, 1}}));
}

TEST_CASE("book: zero quantity and market orders cannot rest") {
    OrderBook book;
    CHECK(!book.addOrder(limit(1, Side::Buy, 100, 0)));
    CHECK(!book.addOrder(market(2, Side::Buy, 10)));
    CHECK(book.empty());
    CHECK(!book.contains(1));
    CHECK(!book.contains(2));
}

TEST_CASE("book: cancel middle order keeps level and FIFO order of the rest") {
    OrderBook book;
    book.addOrder(limit(1, Side::Sell, 50, 1));
    book.addOrder(limit(2, Side::Sell, 50, 2));
    book.addOrder(limit(3, Side::Sell, 50, 3));

    CHECK(book.cancelOrder(2));
    CHECK(!book.contains(2));
    CHECK_EQ(book.depth(Side::Sell, 1), (std::vector<DepthLevel>{{50, 4, 2}}));
    CHECK_EQ(book.bestOrder(Side::Sell)->id, 1);
    book.cancelOrder(1);
    CHECK_EQ(book.bestOrder(Side::Sell)->id, 3);
}

TEST_CASE("book: cancelling the last order at a level removes the level") {
    OrderBook book;
    book.addOrder(limit(1, Side::Buy, 101, 5));
    book.addOrder(limit(2, Side::Buy, 100, 5));
    CHECK(book.cancelOrder(1));
    CHECK_EQ(book.levelCount(Side::Buy), 1);
    CHECK_EQ(book.bestBid(), std::optional<Price>(100));
    CHECK(book.cancelOrder(2));
    CHECK(book.empty());
    CHECK(!book.bestBid().has_value());
}

TEST_CASE("book: cancelling twice / unknown id returns false") {
    OrderBook book;
    book.addOrder(limit(7, Side::Sell, 10, 1));
    CHECK(book.cancelOrder(7));
    CHECK(!book.cancelOrder(7));
    CHECK(!book.cancelOrder(8));
    CHECK(book.empty());
}

TEST_CASE("book: an id can be reused after the order is gone") {
    OrderBook book;
    book.addOrder(limit(1, Side::Buy, 100, 1));
    book.cancelOrder(1);
    CHECK(book.addOrder(limit(1, Side::Sell, 110, 2)));
    CHECK_EQ(book.bestAsk(), std::optional<Price>(110));
}

TEST_CASE("book: reduceBestOrder partial fill keeps order with reduced qty") {
    OrderBook book;
    book.addOrder(limit(1, Side::Sell, 100, 10));
    book.addOrder(limit(2, Side::Sell, 100, 5));

    book.reduceBestOrder(Side::Sell, 4);
    REQUIRE(book.findOrder(1) != nullptr);
    CHECK_EQ(book.findOrder(1)->qty, 6);
    CHECK_EQ(book.bestOrder(Side::Sell)->id, 1);   // still first in line
    CHECK_EQ(book.depth(Side::Sell, 1), (std::vector<DepthLevel>{{100, 11, 2}}));
}

TEST_CASE("book: reduceBestOrder full fill removes order, then level") {
    OrderBook book;
    book.addOrder(limit(1, Side::Buy, 100, 3));
    book.addOrder(limit(2, Side::Buy, 100, 4));
    book.addOrder(limit(3, Side::Buy, 99, 5));

    book.reduceBestOrder(Side::Buy, 3);
    CHECK(!book.contains(1));
    CHECK(!book.cancelOrder(1));                   // index entry is gone too
    CHECK_EQ(book.bestOrder(Side::Buy)->id, 2);

    book.reduceBestOrder(Side::Buy, 4);
    CHECK_EQ(book.levelCount(Side::Buy), 1);
    CHECK_EQ(book.bestBid(), std::optional<Price>(99));
    CHECK_EQ(book.orderCount(), 1);
}

TEST_CASE("book: extreme prices and max quantity") {
    OrderBook book;
    constexpr Quantity kMaxQty = std::numeric_limits<Quantity>::max();
    constexpr Price kLow = std::numeric_limits<Price>::min();
    constexpr Price kHigh = std::numeric_limits<Price>::max();

    REQUIRE(book.addOrder(limit(1, Side::Buy, kLow, kMaxQty)));
    REQUIRE(book.addOrder(limit(2, Side::Sell, kHigh, kMaxQty)));
    REQUIRE(book.addOrder(limit(3, Side::Buy, -5, 1)));   // negative ticks are allowed
    CHECK_EQ(book.bestBid(), std::optional<Price>(-5));
    CHECK_EQ(book.bestAsk(), std::optional<Price>(kHigh));
    CHECK_EQ(book.depth(Side::Buy, 2), (std::vector<DepthLevel>{{-5, 1, 1}, {kLow, kMaxQty, 1}}));

    book.reduceBestOrder(Side::Sell, kMaxQty - 1);
    CHECK_EQ(book.findOrder(2)->qty, 1);
}
