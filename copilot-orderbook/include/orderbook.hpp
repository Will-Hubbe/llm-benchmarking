#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace orderbook {

enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market
};

struct Order {
    std::uint64_t id = 0;
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    std::int64_t price = 0;
    std::uint64_t quantity = 0;
};

struct Trade {
    std::uint64_t buy_order_id = 0;
    std::uint64_t sell_order_id = 0;
    std::int64_t price = 0;
    std::uint64_t quantity = 0;
};

class OrderBook {
public:
    std::vector<Trade> submit(const Order& order);
    bool cancel(std::uint64_t order_id);

    std::optional<std::int64_t> best_bid() const;
    std::optional<std::int64_t> best_ask() const;
    std::uint64_t quantity_at(Side side, std::int64_t price) const;
    std::size_t active_order_count() const;

private:
    using OrderQueue = std::list<Order>;
    using BidLevels = std::map<std::int64_t, OrderQueue, std::greater<std::int64_t>>;
    using AskLevels = std::map<std::int64_t, OrderQueue>;

    struct OrderLocation {
        Side side;
        std::int64_t price;
        OrderQueue::iterator iterator;
    };

    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<std::uint64_t, OrderLocation> order_locations_;
};

enum class Workload {
    Balanced,
    Aggressive,
    CancelHeavy
};

enum class EventKind {
    Submit,
    Cancel
};

struct Event {
    EventKind kind = EventKind::Submit;
    Order order;
    std::uint64_t order_id = 0;
};

struct FeedConfig {
    std::size_t event_count = 10'000;
    std::uint64_t seed = 42;
    Workload workload = Workload::Balanced;
};

struct ReplayStats {
    std::size_t events_processed = 0;
    std::size_t orders_submitted = 0;
    std::size_t cancellations_succeeded = 0;
    std::size_t trades = 0;
};

std::vector<Event> generate_synthetic_feed(const FeedConfig& config);
ReplayStats replay_events(OrderBook& book, const std::vector<Event>& events);
Workload parse_workload(const char* value);
const char* workload_name(Workload workload);

} // namespace orderbook
