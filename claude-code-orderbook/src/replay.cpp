#include "orderbook/replay.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ob {

ReplayStats replay(MatchingEngine& engine, std::span<const Event> events, std::vector<Trade>* allTrades) {
    ReplayStats stats;
    if (allTrades != nullptr) {
        for (const Event& ev : events) applyEvent(engine, ev, *allTrades, stats);
        return stats;
    }
    std::vector<Trade> scratch;
    scratch.reserve(1024);
    for (const Event& ev : events) {
        applyEvent(engine, ev, scratch, stats);
        scratch.clear();   // keeps capacity: no reallocation in steady state
    }
    return stats;
}

// ---- CSV --------------------------------------------------------------------

namespace {

constexpr std::string_view kEventsHeader = "type,id,side,order_type,price,qty";
constexpr std::string_view kTradesHeader = "buy_id,sell_id,price,qty,aggressor";

std::ofstream openOut(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open for writing: " + path.string());
    return out;
}

[[noreturn]] void parseError(std::size_t line, const std::string& msg) {
    throw std::runtime_error("events csv line " + std::to_string(line) + ": " + msg);
}

template <class Int>
Int parseInt(std::string_view s, std::size_t line, const char* field) {
    Int v{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size())
        parseError(line, std::string("bad ") + field + " '" + std::string(s) + "'");
    return v;
}

}  // namespace

void saveEventsCsv(const std::filesystem::path& path, std::span<const Event> events) {
    std::ofstream out = openOut(path);
    out << kEventsHeader << '\n';
    for (const Event& e : events) {
        const Order& o = e.order;
        if (e.type == EventType::Cancel) {
            out << "cancel," << o.id << ",,,,\n";
        } else {
            out << "add," << o.id << ',' << (o.side == Side::Buy ? "buy" : "sell") << ','
                << (o.type == OrderType::Limit ? "limit" : "market") << ',' << o.price << ','
                << o.qty << '\n';
        }
    }
    if (!out) throw std::runtime_error("write failed: " + path.string());
}

std::vector<Event> loadEventsCsv(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open for reading: " + path.string());

    std::vector<Event> events;
    std::string raw;
    std::size_t lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        std::string_view line = raw;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || (lineNo == 1 && line == kEventsHeader)) continue;

        std::array<std::string_view, 6> f{};
        std::size_t n = 0, start = 0;
        for (std::size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == ',') {
                if (n == f.size()) parseError(lineNo, "too many fields");
                f[n++] = line.substr(start, i - start);
                start = i + 1;
            }
        }
        if (n != f.size()) parseError(lineNo, "expected 6 fields, got " + std::to_string(n));

        const auto id = parseInt<OrderId>(f[1], lineNo, "id");
        if (f[0] == "cancel") {
            events.push_back(Event::cancel(id));
            continue;
        }
        if (f[0] != "add") parseError(lineNo, "unknown type '" + std::string(f[0]) + "'");

        Order o;
        o.id = id;
        if (f[2] == "buy") o.side = Side::Buy;
        else if (f[2] == "sell") o.side = Side::Sell;
        else parseError(lineNo, "bad side '" + std::string(f[2]) + "'");
        if (f[3] == "limit") o.type = OrderType::Limit;
        else if (f[3] == "market") o.type = OrderType::Market;
        else parseError(lineNo, "bad order_type '" + std::string(f[3]) + "'");
        o.price = parseInt<Price>(f[4], lineNo, "price");
        o.qty = parseInt<Quantity>(f[5], lineNo, "qty");
        events.push_back(Event::add(o));
    }
    return events;
}

void saveTradesCsv(const std::filesystem::path& path, std::span<const Trade> trades) {
    std::ofstream out = openOut(path);
    out << kTradesHeader << '\n';
    for (const Trade& t : trades) {
        out << t.buyOrderId << ',' << t.sellOrderId << ',' << t.price << ',' << t.qty << ','
            << (t.aggressor == Side::Buy ? "buy" : "sell") << '\n';
    }
    if (!out) throw std::runtime_error("write failed: " + path.string());
}

}  // namespace ob
