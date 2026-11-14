#include "orderbook.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string_view>

namespace orderbook {
namespace {

constexpr std::int64_t kReferencePrice = 10'000;

Side random_side(std::mt19937_64& random) {
    return std::uniform_int_distribution<int>(0, 1)(random) == 0
        ? Side::Buy
        : Side::Sell;
}

Order make_limit_order(
    std::mt19937_64& random,
    std::uint64_t id,
    Side side,
    bool marketable) {
    const std::uint64_t quantity = std::uniform_int_distribution<std::uint64_t>(1, 50)(random);
    std::int64_t price = 0;
    if (marketable) {
        const std::int64_t offset = std::uniform_int_distribution<std::int64_t>(1, 20)(random);
        price = side == Side::Buy ? kReferencePrice + offset : kReferencePrice - offset;
    } else if (side == Side::Buy) {
        price = kReferencePrice - std::uniform_int_distribution<std::int64_t>(1, 50)(random);
    } else {
        price = kReferencePrice + std::uniform_int_distribution<std::int64_t>(1, 50)(random);
    }
    return Order{id, side, OrderType::Limit, price, quantity};
}

Order make_market_order(std::mt19937_64& random, std::uint64_t id) {
    return Order{
        id,
        random_side(random),
        OrderType::Market,
        0,
        std::uniform_int_distribution<std::uint64_t>(1, 50)(random)
    };
}

Event make_submission(
    std::mt19937_64& random,
    std::uint64_t id,
    bool aggressive) {
    if (aggressive) {
        const int choice = std::uniform_int_distribution<int>(0, 99)(random);
        if (choice < 60) {
            return Event{EventKind::Submit, make_market_order(random, id), 0};
        }
        if (choice < 85) {
            return Event{
                EventKind::Submit,
                make_limit_order(random, id, random_side(random), true),
                0
            };
        }
    }
    return Event{
        EventKind::Submit,
        make_limit_order(random, id, random_side(random), false),
        0
    };
}

} // namespace

std::vector<Event> generate_synthetic_feed(const FeedConfig& config) {
    std::mt19937_64 random(config.seed);
    std::vector<Event> events;
    events.reserve(config.event_count);
    std::vector<std::uint64_t> cancellation_candidates;
    std::uint64_t next_order_id = 1;

    for (std::size_t index = 0; index < config.event_count; ++index) {
        int cancellation_chance = 0;
        switch (config.workload) {
        case Workload::Balanced:
            cancellation_chance = 30;
            break;
        case Workload::Aggressive:
            cancellation_chance = 10;
            break;
        case Workload::CancelHeavy:
            cancellation_chance = 80;
            break;
        }

        const bool can_cancel = !cancellation_candidates.empty();
        const bool should_cancel =
            can_cancel && std::uniform_int_distribution<int>(0, 99)(random) < cancellation_chance;
        if (should_cancel) {
            const std::size_t candidate_index =
                std::uniform_int_distribution<std::size_t>(
                    0, cancellation_candidates.size() - 1)(random);
            const std::uint64_t canceled_id = cancellation_candidates[candidate_index];
            cancellation_candidates[candidate_index] = cancellation_candidates.back();
            cancellation_candidates.pop_back();
            events.push_back(Event{EventKind::Cancel, {}, canceled_id});
            continue;
        }

        const std::uint64_t id = next_order_id++;
        const bool aggressive = config.workload == Workload::Aggressive;
        Event event = make_submission(random, id, aggressive);
        if (event.order.type == OrderType::Limit) {
            cancellation_candidates.push_back(id);
        }
        events.push_back(event);
    }

    return events;
}

ReplayStats replay_events(OrderBook& book, const std::vector<Event>& events) {
    ReplayStats stats;
    for (const Event& event : events) {
        ++stats.events_processed;
        if (event.kind == EventKind::Submit) {
            ++stats.orders_submitted;
            stats.trades += book.submit(event.order).size();
        } else if (book.cancel(event.order_id)) {
            ++stats.cancellations_succeeded;
        }
    }
    return stats;
}

Workload parse_workload(const char* value) {
    const std::string_view name(value);
    if (name == "balanced") {
        return Workload::Balanced;
    }
    if (name == "aggressive") {
        return Workload::Aggressive;
    }
    if (name == "cancel-heavy") {
        return Workload::CancelHeavy;
    }
    throw std::invalid_argument("workload must be balanced, aggressive, or cancel-heavy");
}

const char* workload_name(Workload workload) {
    switch (workload) {
    case Workload::Balanced:
        return "balanced";
    case Workload::Aggressive:
        return "aggressive";
    case Workload::CancelHeavy:
        return "cancel-heavy";
    }
    throw std::invalid_argument("unknown workload");
}

} // namespace orderbook
