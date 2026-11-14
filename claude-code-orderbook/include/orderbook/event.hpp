#pragma once

#include <cstdint>

#include "orderbook/order.hpp"

namespace ob {

enum class EventType : std::uint8_t { Add, Cancel };

// One message in an order feed. For Cancel events only `order.id` is used
// and every other field is left at its default value.
struct Event {
    EventType type = EventType::Add;
    Order order;

    static Event add(const Order& o) { return Event{EventType::Add, o}; }
    static Event cancel(OrderId id) {
        Event e{EventType::Cancel, Order{}};
        e.order.id = id;
        return e;
    }

    bool operator==(const Event&) const = default;
};

}  // namespace ob
