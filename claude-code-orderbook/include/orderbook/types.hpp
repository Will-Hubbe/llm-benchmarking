#pragma once

#include <cstdint>

namespace ob {

// Prices are integer ticks (e.g. cents). No floating point anywhere in the book,
// so price comparisons are exact and two orders at "the same price" really are.
using Price    = std::int64_t;
using Quantity = std::uint64_t;
using OrderId  = std::uint64_t;
using SeqNum   = std::uint64_t;   // arrival sequence number -> time priority

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };

constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

}  // namespace ob
