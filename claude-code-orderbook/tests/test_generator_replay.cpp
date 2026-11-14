// Synthetic feed generator, replay, and CSV persistence.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "orderbook/feed_generator.hpp"
#include "orderbook/replay.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace ob;

#ifndef OB_TEST_OUTPUT_DIR
#error "OB_TEST_OUTPUT_DIR must be defined by CMake (a folder inside build/)"
#endif

namespace {
std::filesystem::path outDir() {
    std::filesystem::path p = OB_TEST_OUTPUT_DIR;
    std::filesystem::create_directories(p);
    return p;
}

std::size_t countCancels(const std::vector<Event>& ev) {
    std::size_t n = 0;
    for (const auto& e : ev) n += e.type == EventType::Cancel;
    return n;
}
}  // namespace

// ---- Generator: determinism -------------------------------------------------

TEST_CASE("gen: same seed produces an identical feed") {
    auto a = generateFeed(FeedConfig::balanced(20'000, 7));
    auto b = generateFeed(FeedConfig::balanced(20'000, 7));
    CHECK(a == b);
}

TEST_CASE("gen: different seeds produce different feeds") {
    auto a = generateFeed(FeedConfig::balanced(20'000, 7));
    auto b = generateFeed(FeedConfig::balanced(20'000, 8));
    CHECK(a != b);
}

TEST_CASE("gen: a shorter feed is a prefix of a longer one with the same seed") {
    auto shortFeed = generateFeed(FeedConfig::aggressive(1'000, 3));
    auto longFeed = generateFeed(FeedConfig::aggressive(5'000, 3));
    REQUIRE(longFeed.size() == 5'000);
    CHECK(std::vector<Event>(longFeed.begin(), longFeed.begin() + 1'000) == shortFeed);
}

TEST_CASE("gen: golden values pin the exact output for seed 42") {
    // If this fails, the generator's output changed: benchmark results from
    // before and after the change are no longer comparable.
    auto ev = generateFeed(FeedConfig::balanced(1'000, 42));
    ReplayStats s;
    for (const auto& e : ev) {
        s.checksum = detail::mix(s.checksum, static_cast<std::uint64_t>(e.type));
        s.checksum = detail::mix(s.checksum, e.order.id);
        s.checksum = detail::mix(s.checksum, static_cast<std::uint64_t>(e.order.price));
        s.checksum = detail::mix(s.checksum, e.order.qty);
    }
    CHECK_EQ(s.checksum, 2873558196521584405ull);   // recorded 2026-11-14, MSVC 14.29
}

// ---- Generator: shape of the feed -------------------------------------------

TEST_CASE("gen: exact event count, ids unique and increasing") {
    auto ev = generateFeed(FeedConfig::balanced(50'000, 1));
    CHECK_EQ(ev.size(), 50'000);
    OrderId last = 0;
    bool increasing = true;
    for (const auto& e : ev) {
        if (e.type != EventType::Add) continue;
        increasing = increasing && e.order.id > last;
        last = e.order.id;
    }
    CHECK(increasing);
}

TEST_CASE("gen: cancels target previously issued limit ids, each at most once") {
    auto ev = generateFeed(FeedConfig::cancelHeavy(50'000, 2));
    std::unordered_set<OrderId> issuedLimits, cancelled;
    bool ok = true;
    for (const auto& e : ev) {
        if (e.type == EventType::Add) {
            if (e.order.type == OrderType::Limit) issuedLimits.insert(e.order.id);
            continue;
        }
        ok = ok && issuedLimits.contains(e.order.id) && cancelled.insert(e.order.id).second;
    }
    CHECK(ok);
    CHECK(!cancelled.empty());
}

TEST_CASE("gen: event mix matches configured ratios (within 1.5%)") {
    for (const auto& cfg : {FeedConfig::balanced(100'000, 5), FeedConfig::aggressive(100'000, 5)}) {
        auto ev = generateFeed(cfg);
        std::size_t markets = 0;
        for (const auto& e : ev) markets += e.type == EventType::Add && e.order.type == OrderType::Market;
        const double n = static_cast<double>(ev.size());
        CHECK(std::abs(static_cast<double>(countCancels(ev)) / n - cfg.cancelRatio) < 0.015);
        CHECK(std::abs(static_cast<double>(markets) / n - cfg.marketRatio) < 0.015);
    }
}

TEST_CASE("gen: prices and quantities stay within configured bounds") {
    FeedConfig cfg = FeedConfig::balanced(50'000, 9);
    cfg.minQty = 5;
    cfg.maxQty = 7;
    auto ev = generateFeed(cfg);
    bool ok = true, sawMin = false, sawMax = false;
    for (const auto& e : ev) {
        if (e.type != EventType::Add) continue;
        const Order& o = e.order;
        ok = ok && o.qty >= 5 && o.qty <= 7;
        sawMin = sawMin || o.qty == 5;
        sawMax = sawMax || o.qty == 7;
        if (o.type == OrderType::Limit)
            ok = ok && o.price >= cfg.midPrice - cfg.priceRange && o.price <= cfg.midPrice + cfg.priceRange;
    }
    CHECK(ok);
    CHECK(sawMin && sawMax);   // both ends of the range are reachable
}

TEST_CASE("gen: with no aggressive orders, passive buys never cross passive sells") {
    FeedConfig cfg = FeedConfig::balanced(20'000, 4);
    cfg.aggressiveRatio = 0.0;
    cfg.marketRatio = 0.0;
    auto ev = generateFeed(cfg);
    bool ok = true;
    for (const auto& e : ev) {
        if (e.type != EventType::Add) continue;
        ok = ok && (e.order.side == Side::Buy ? e.order.price < cfg.midPrice : e.order.price > cfg.midPrice);
    }
    CHECK(ok);
    MatchingEngine eng;
    CHECK_EQ(replay(eng, ev).trades, 0);
}

TEST_CASE("gen: invalid configs are rejected") {
    auto throws = [](FeedConfig c) {
        try { generateFeed(c); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    FeedConfig c;
    c.cancelRatio = 0.7; c.marketRatio = 0.4;   CHECK(throws(c));
    c = {}; c.aggressiveRatio = 1.5;            CHECK(throws(c));
    c = {}; c.minQty = 0;                       CHECK(throws(c));
    c = {}; c.minQty = 10; c.maxQty = 5;        CHECK(throws(c));
    c = {}; c.priceRange = 0;                   CHECK(throws(c));
    c = {}; c.numEvents = 0;                    CHECK(!throws(c));
}

// ---- Replay -----------------------------------------------------------------

TEST_CASE("replay: stats are internally consistent") {
    auto ev = generateFeed(FeedConfig::balanced(30'000, 11));
    MatchingEngine eng;
    std::vector<Trade> trades;
    auto s = replay(eng, ev, &trades);

    CHECK_EQ(s.ordersSubmitted + s.cancelsRequested, ev.size());
    CHECK_EQ(s.cancelsRequested, countCancels(ev));
    CHECK(s.cancelsSucceeded <= s.cancelsRequested);
    CHECK(s.cancelsSucceeded > 0);
    CHECK_EQ(s.ordersRejected, 0);   // generator only emits valid orders
    CHECK_EQ(s.trades, trades.size());
    Quantity vol = 0;
    for (const auto& t : trades) vol += t.qty;
    CHECK_EQ(s.volume, vol);
    CHECK(s.trades > 0);
}

TEST_CASE("replay: identical runs give identical stats and checksum") {
    auto ev = generateFeed(FeedConfig::aggressive(30'000, 12));
    MatchingEngine a, b;
    std::vector<Trade> collected;
    auto sa = replay(a, ev);              // internal scratch buffer path
    auto sb = replay(b, ev, &collected);  // collect-all path
    CHECK_EQ(sa, sb);
    CHECK_EQ(a.book().depth(Side::Buy, SIZE_MAX), b.book().depth(Side::Buy, SIZE_MAX));
    CHECK_EQ(a.book().depth(Side::Sell, SIZE_MAX), b.book().depth(Side::Sell, SIZE_MAX));
}

TEST_CASE("replay: matches a hand-written loop over the engine") {
    auto ev = generateFeed(FeedConfig::cancelHeavy(20'000, 13));
    MatchingEngine viaReplay, manual;
    std::vector<Trade> t1, t2;
    replay(viaReplay, ev, &t1);
    std::size_t cancelOk = 0;
    for (const auto& e : ev) {
        if (e.type == EventType::Cancel) cancelOk += manual.cancel(e.order.id);
        else manual.submit(e.order, t2);
    }
    CHECK(t1 == t2);
    CHECK(cancelOk > 0);
    CHECK_EQ(viaReplay.book().orderCount(), manual.book().orderCount());
}

TEST_CASE("replay: checksum is sensitive to trade order and content") {
    auto ev = generateFeed(FeedConfig::balanced(10'000, 14));
    MatchingEngine eng;
    std::vector<Trade> trades;
    auto s = replay(eng, ev, &trades);
    REQUIRE(trades.size() >= 2);

    auto checksumOf = [](const std::vector<Trade>& ts) {
        std::uint64_t h = 0;
        for (const auto& t : ts) {
            h = detail::mix(h, t.buyOrderId); h = detail::mix(h, t.sellOrderId);
            h = detail::mix(h, static_cast<std::uint64_t>(t.price)); h = detail::mix(h, t.qty);
        }
        return h;
    };
    CHECK_EQ(checksumOf(trades), s.checksum);
    auto swapped = trades;
    std::swap(swapped[0], swapped[1]);
    CHECK(checksumOf(swapped) != s.checksum);
    auto tweaked = trades;
    tweaked.back().qty += 1;
    CHECK(checksumOf(tweaked) != s.checksum);
}

TEST_CASE("replay: wide-book workload really spreads over many levels") {
    auto ev = generateFeed(FeedConfig::wideBook(50'000, 15));
    MatchingEngine eng;
    replay(eng, ev);
    const auto levels = eng.book().levelCount(Side::Buy) + eng.book().levelCount(Side::Sell);
    std::printf("    (wide-book: %zu live levels, %zu live orders)\n", levels, eng.book().orderCount());
    CHECK(levels > 2'000);
}

TEST_CASE("replay: empty feed is a no-op") {
    MatchingEngine eng;
    CHECK_EQ(replay(eng, std::span<const Event>{}), ReplayStats{});
    CHECK(eng.book().empty());
}

// ---- CSV --------------------------------------------------------------------

TEST_CASE("csv: events round-trip exactly (incl. negative prices, max values)") {
    auto ev = generateFeed(FeedConfig::balanced(5'000, 16));
    Order edge;
    edge.id = std::numeric_limits<OrderId>::max();
    edge.side = Side::Sell;
    edge.price = std::numeric_limits<Price>::min();
    edge.qty = std::numeric_limits<Quantity>::max();
    ev.push_back(Event::add(edge));
    ev.push_back(Event::cancel(std::numeric_limits<OrderId>::max()));

    const auto path = outDir() / "roundtrip_events.csv";
    saveEventsCsv(path, ev);
    CHECK(loadEventsCsv(path) == ev);
}

TEST_CASE("csv: replay of a loaded file gives the same result as the original") {
    auto ev = generateFeed(FeedConfig::aggressive(10'000, 17));
    const auto path = outDir() / "replay_events.csv";
    saveEventsCsv(path, ev);
    auto loaded = loadEventsCsv(path);
    MatchingEngine a, b;
    CHECK_EQ(replay(a, ev), replay(b, loaded));
}

TEST_CASE("csv: trades file is written with header and one row per trade") {
    auto ev = generateFeed(FeedConfig::aggressive(2'000, 18));
    MatchingEngine eng;
    std::vector<Trade> trades;
    replay(eng, ev, &trades);
    const auto path = outDir() / "trades.csv";
    saveTradesCsv(path, trades);
    std::ifstream in(path);
    std::string line;
    std::size_t lines = 0;
    std::getline(in, line);
    CHECK_EQ(line, std::string("buy_id,sell_id,price,qty,aggressor"));
    while (std::getline(in, line)) ++lines;
    CHECK_EQ(lines, trades.size());
}

TEST_CASE("csv: malformed input reports the offending line") {
    const auto path = outDir() / "bad_events.csv";
    auto expectError = [&](const char* body, const char* needle) {
        { std::ofstream(path, std::ios::binary | std::ios::trunc) << body; }
        try { loadEventsCsv(path); } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find(needle) != std::string::npos) return true;
            std::printf("    unexpected message: %s\n", e.what());
            return false;
        }
        return false;
    };
    CHECK(expectError("type,id,side,order_type,price,qty\nadd,1,buy,limit,100,5\nadd,2,buy,limit,abc,5\n", "line 3"));
    CHECK(expectError("add,1,up,limit,100,5\n", "bad side"));
    CHECK(expectError("add,1,buy,stop,100,5\n", "bad order_type"));
    CHECK(expectError("modify,1,buy,limit,100,5\n", "unknown type"));
    CHECK(expectError("add,1,buy,limit,100\n", "expected 6 fields"));
    CHECK(expectError("add,1,buy,limit,100,5,9\n", "too many fields"));
    CHECK(expectError("add,1,buy,limit,100,-5\n", "bad qty"));
}

TEST_CASE("csv: missing file throws") {
    bool threw = false;
    try { loadEventsCsv(outDir() / "does_not_exist.csv"); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}

TEST_CASE("csv: CRLF line endings are accepted") {
    const auto path = outDir() / "crlf_events.csv";
    { std::ofstream(path, std::ios::binary | std::ios::trunc) << "type,id,side,order_type,price,qty\r\nadd,1,buy,limit,100,5\r\ncancel,1,,,,\r\n"; }
    auto ev = loadEventsCsv(path);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0] == Event::add(testutil::limit(1, Side::Buy, 100, 5)));
    CHECK(ev[1] == Event::cancel(1));
}
