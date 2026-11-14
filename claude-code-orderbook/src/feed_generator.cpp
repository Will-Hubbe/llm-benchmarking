#include "orderbook/feed_generator.hpp"

#include <random>
#include <stdexcept>

namespace ob {

namespace {

// Thin wrapper over mt19937_64 with portable, hand-rolled conversions.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : gen_(seed) {}

    // Uniform double in [0, 1) using the top 53 bits.
    double unit() { return static_cast<double>(gen_() >> 11) * 0x1.0p-53; }

    // Uniform integer in [lo, hi]. Modulo bias is < 2^-40 for our tiny ranges.
    std::uint64_t between(std::uint64_t lo, std::uint64_t hi) { return lo + gen_() % (hi - lo + 1); }

    bool coin() { return (gen_() >> 63) != 0; }

private:
    std::mt19937_64 gen_;
};

void validate(const FeedConfig& c) {
    auto prob = [](double p) { return p >= 0.0 && p <= 1.0; };
    if (!prob(c.cancelRatio) || !prob(c.marketRatio) || !prob(c.aggressiveRatio))
        throw std::invalid_argument("FeedConfig: ratios must be in [0, 1]");
    if (c.cancelRatio + c.marketRatio > 1.0)
        throw std::invalid_argument("FeedConfig: cancelRatio + marketRatio must be <= 1");
    if (c.minQty == 0 || c.minQty > c.maxQty)
        throw std::invalid_argument("FeedConfig: need 1 <= minQty <= maxQty");
    if (c.priceRange < 1) throw std::invalid_argument("FeedConfig: priceRange must be >= 1");
}

}  // namespace

FeedConfig FeedConfig::balanced(std::size_t n, std::uint64_t seed) {
    FeedConfig c;
    c.name = "balanced"; c.numEvents = n; c.seed = seed;
    c.cancelRatio = 0.20; c.marketRatio = 0.05; c.aggressiveRatio = 0.10; c.priceRange = 50;
    return c;
}

FeedConfig FeedConfig::cancelHeavy(std::size_t n, std::uint64_t seed) {
    FeedConfig c;
    c.name = "cancel-heavy"; c.numEvents = n; c.seed = seed;
    // 45% of all messages are cancels. Kept below the add rate on purpose: at
    // >= 50% the feed cancels orders as fast as it adds them and the book stays
    // nearly empty, which would benchmark a trivial book.
    c.cancelRatio = 0.45; c.marketRatio = 0.02; c.aggressiveRatio = 0.05; c.priceRange = 50;
    return c;
}

FeedConfig FeedConfig::aggressive(std::size_t n, std::uint64_t seed) {
    FeedConfig c;
    c.name = "aggressive"; c.numEvents = n; c.seed = seed;
    c.cancelRatio = 0.10; c.marketRatio = 0.15; c.aggressiveRatio = 0.40; c.priceRange = 10;
    return c;
}

FeedConfig FeedConfig::wideBook(std::size_t n, std::uint64_t seed) {
    FeedConfig c;
    c.name = "wide-book"; c.numEvents = n; c.seed = seed;
    c.cancelRatio = 0.20; c.marketRatio = 0.02; c.aggressiveRatio = 0.05; c.priceRange = 2'000;
    return c;
}

std::vector<Event> generateFeed(const FeedConfig& cfg) {
    validate(cfg);
    Rng rng(cfg.seed);

    std::vector<Event> events;
    events.reserve(cfg.numEvents);
    std::vector<OrderId> live;      // issued limit ids not yet cancelled by the feed
    live.reserve(cfg.numEvents);
    OrderId nextId = 1;
    const auto range = static_cast<std::uint64_t>(cfg.priceRange);

    for (std::size_t i = 0; i < cfg.numEvents; ++i) {
        const double u = rng.unit();

        if (u < cfg.cancelRatio && !live.empty()) {
            // Pick a random live id; swap-remove so each id is cancelled at most once.
            const auto idx = static_cast<std::size_t>(rng.between(0, live.size() - 1));
            events.push_back(Event::cancel(live[idx]));
            live[idx] = live.back();
            live.pop_back();
            continue;
        }

        Order o;
        o.id = nextId++;
        o.side = rng.coin() ? Side::Buy : Side::Sell;
        o.qty = rng.between(cfg.minQty, cfg.maxQty);

        const bool isMarket = u >= cfg.cancelRatio && u < cfg.cancelRatio + cfg.marketRatio;
        if (isMarket) {
            o.type = OrderType::Market;
        } else {
            // Includes the "cancel requested but nothing to cancel" case.
            o.type = OrderType::Limit;
            const bool aggr = rng.unit() < cfg.aggressiveRatio;
            const auto offset = static_cast<Price>(rng.between(aggr ? 0 : 1, range));
            const bool above = (o.side == Side::Buy) == aggr;   // buy-aggr / sell-passive go above mid
            o.price = above ? cfg.midPrice + offset : cfg.midPrice - offset;
            live.push_back(o.id);
        }
        events.push_back(Event::add(o));
    }
    return events;
}

}  // namespace ob
