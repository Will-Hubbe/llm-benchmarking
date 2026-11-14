#include "orderbook.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

orderbook::Order limit(
    std::uint64_t id,
    orderbook::Side side,
    std::int64_t price,
    std::uint64_t quantity) {
    return {id, side, orderbook::OrderType::Limit, price, quantity};
}

orderbook::Order market(
    std::uint64_t id,
    orderbook::Side side,
    std::uint64_t quantity) {
    return {id, side, orderbook::OrderType::Market, 0, quantity};
}

void test_add_orders_and_best_prices() {
    orderbook::OrderBook book;
    book.submit(limit(1, orderbook::Side::Buy, 99, 5));
    book.submit(limit(2, orderbook::Side::Sell, 105, 7));
    book.submit(limit(3, orderbook::Side::Buy, 101, 2));
    book.submit(limit(4, orderbook::Side::Sell, 103, 3));

    expect(book.best_bid() == 101, "best bid should be the highest buy price");
    expect(book.best_ask() == 103, "best ask should be the lowest sell price");
    expect(book.quantity_at(orderbook::Side::Buy, 99) == 5, "buy level quantity should be available");
    expect(book.quantity_at(orderbook::Side::Sell, 105) == 7, "sell level quantity should be available");
}

void test_crossing_order_and_partial_fill() {
    orderbook::OrderBook book;
    book.submit(limit(1, orderbook::Side::Sell, 100, 8));

    const auto trades = book.submit(limit(2, orderbook::Side::Buy, 100, 3));
    expect(trades.size() == 1, "crossing orders should produce one trade");
    expect(trades[0].price == 100 && trades[0].quantity == 3, "trade should use resting price and fill quantity");
    expect(book.quantity_at(orderbook::Side::Sell, 100) == 5, "partial maker fill should leave the remainder");

    const auto second_trades = book.submit(limit(3, orderbook::Side::Buy, 100, 7));
    expect(second_trades.size() == 1 && second_trades[0].quantity == 5,
           "later order should fill the remaining resting quantity");
    expect(!book.best_ask().has_value(), "fully filled ask should be removed");
    expect(book.quantity_at(orderbook::Side::Buy, 100) == 2,
           "unfilled limit quantity should rest on the book");
}

void test_price_time_priority_and_multiple_levels() {
    orderbook::OrderBook book;
    book.submit(limit(1, orderbook::Side::Sell, 102, 4));
    book.submit(limit(2, orderbook::Side::Sell, 100, 2));
    book.submit(limit(3, orderbook::Side::Sell, 100, 3));

    const auto trades = book.submit(market(4, orderbook::Side::Buy, 6));
    expect(trades.size() == 3, "market order should match across multiple price levels");
    expect(trades[0].sell_order_id == 2 && trades[0].quantity == 2,
           "earlier order at best price should fill first");
    expect(trades[1].sell_order_id == 3 && trades[1].quantity == 3,
           "next order at the same price should fill second");
    expect(trades[2].sell_order_id == 1 && trades[2].price == 102 &&
               trades[2].quantity == 1,
           "matching should continue to the next price level in price priority order");
    expect(book.best_ask() == 102, "remaining worse-priced ask should become the best ask");
    expect(book.quantity_at(orderbook::Side::Sell, 102) == 3,
           "partial fill should update quantity at the next price level");
}

void test_market_orders_do_not_rest() {
    orderbook::OrderBook book;
    const auto no_liquidity = book.submit(market(1, orderbook::Side::Buy, 5));
    expect(no_liquidity.empty(), "market order with no liquidity should not trade");
    expect(book.active_order_count() == 0, "unfilled market order should not rest");

    book.submit(limit(2, orderbook::Side::Buy, 99, 4));
    const auto trades = book.submit(market(3, orderbook::Side::Sell, 6));
    expect(trades.size() == 1 && trades[0].quantity == 4,
           "market sell should execute available quantity");
    expect(book.active_order_count() == 0, "market remainder should not rest");
}

void test_cancel_and_best_price_updates() {
    orderbook::OrderBook book;
    book.submit(limit(1, orderbook::Side::Buy, 99, 4));
    book.submit(limit(2, orderbook::Side::Buy, 101, 2));
    book.submit(limit(3, orderbook::Side::Sell, 105, 3));
    book.submit(limit(4, orderbook::Side::Sell, 103, 1));

    expect(book.cancel(2), "cancel should find an existing order");
    expect(!book.cancel(999), "cancel should report a missing order");
    expect(book.best_bid() == 99, "best bid should update after cancel");
    expect(book.cancel(4), "second cancel should find an existing order");
    expect(book.best_ask() == 105, "best ask should update after cancel");
    expect(book.active_order_count() == 2, "only uncanceled orders should remain");
}

void test_seeded_feed_is_reproducible() {
    const orderbook::FeedConfig config{100, 123, orderbook::Workload::Aggressive};
    const auto first = orderbook::generate_synthetic_feed(config);
    const auto second = orderbook::generate_synthetic_feed(config);
    expect(first.size() == second.size(), "generated feeds should have the configured length");
    for (std::size_t index = 0; index < first.size(); ++index) {
        expect(first[index].kind == second[index].kind, "feed event kinds should be reproducible");
        expect(first[index].order_id == second[index].order_id, "feed cancellation IDs should be reproducible");
        expect(first[index].order.id == second[index].order.id, "feed order IDs should be reproducible");
        expect(first[index].order.price == second[index].order.price, "feed prices should be reproducible");
        expect(first[index].order.quantity == second[index].order.quantity,
               "feed quantities should be reproducible");
    }
}

} // namespace

int main() {
    try {
        test_add_orders_and_best_prices();
        test_crossing_order_and_partial_fill();
        test_price_time_priority_and_multiple_levels();
        test_market_orders_do_not_rest();
        test_cancel_and_best_price_updates();
        test_seeded_feed_is_reproducible();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All order book tests passed.\n";
    return 0;
}
