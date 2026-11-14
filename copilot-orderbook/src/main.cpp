#include "orderbook.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

template <typename Integer>
Integer parse_integer(const char* value, const char* option_name) {
    const std::string_view text(value);
    Integer result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument(std::string("invalid value for ") + option_name);
    }
    return result;
}

void print_usage(const char* executable) {
    std::cout << "Usage: " << executable
              << " [--events COUNT] [--seed SEED]"
                 " [--workload balanced|aggressive|cancel-heavy]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    orderbook::FeedConfig config;
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string_view option(argv[index]);
            if (option == "--help") {
                print_usage(argv[0]);
                return 0;
            }
            if (index + 1 >= argc) {
                throw std::invalid_argument("missing value for option " + std::string(option));
            }

            const char* value = argv[++index];
            if (option == "--events") {
                config.event_count = parse_integer<std::size_t>(value, "--events");
            } else if (option == "--seed") {
                config.seed = parse_integer<std::uint64_t>(value, "--seed");
            } else if (option == "--workload") {
                config.workload = orderbook::parse_workload(value);
            } else {
                throw std::invalid_argument("unknown option " + std::string(option));
            }
        }

        const std::vector<orderbook::Event> events =
            orderbook::generate_synthetic_feed(config);
        orderbook::OrderBook book;
        const orderbook::ReplayStats stats = orderbook::replay_events(book, events);

        std::cout << "Workload: " << orderbook::workload_name(config.workload) << '\n'
                  << "Seed: " << config.seed << '\n'
                  << "Events processed: " << stats.events_processed << '\n'
                  << "Orders submitted: " << stats.orders_submitted << '\n'
                  << "Cancellations succeeded: " << stats.cancellations_succeeded << '\n'
                  << "Trades: " << stats.trades << '\n'
                  << "Active orders remaining: " << book.active_order_count() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        print_usage(argv[0]);
        return 1;
    }
    return 0;
}
