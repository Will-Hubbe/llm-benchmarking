#include "orderbook.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
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
              << " [--seed SEED] [--workload balanced|aggressive|cancel-heavy]\n";
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
            if (option == "--seed") {
                config.seed = parse_integer<std::uint64_t>(value, "--seed");
            } else if (option == "--workload") {
                config.workload = orderbook::parse_workload(value);
            } else {
                throw std::invalid_argument("unknown option " + std::string(option));
            }
        }

        constexpr std::array<std::size_t, 3> event_counts{
            10'000, 100'000, 1'000'000
        };
        std::cout << "Workload: " << orderbook::workload_name(config.workload)
                  << ", seed: " << config.seed << '\n'
                  << std::left << std::setw(14) << "Events"
                  << std::right << std::setw(18) << "Time (seconds)"
                  << std::setw(20) << "Events/second"
                  << std::setw(24) << "Avg time (ns/event)" << '\n';
        std::cout << std::fixed << std::setprecision(6);

        for (const std::size_t event_count : event_counts) {
            config.event_count = event_count;
            const std::vector<orderbook::Event> events =
                orderbook::generate_synthetic_feed(config);

            orderbook::OrderBook book;
            const auto start = std::chrono::steady_clock::now();
            const orderbook::ReplayStats stats = orderbook::replay_events(book, events);
            const auto stop = std::chrono::steady_clock::now();

            const double seconds =
                std::chrono::duration<double>(stop - start).count();
            const double events_per_second =
                seconds > 0.0 ? static_cast<double>(stats.events_processed) / seconds : 0.0;
            const double average_nanoseconds =
                stats.events_processed > 0
                    ? seconds * 1'000'000'000.0 /
                        static_cast<double>(stats.events_processed)
                    : 0.0;

            std::cout << std::left << std::setw(14) << stats.events_processed
                      << std::right << std::setw(18) << seconds
                      << std::setw(20) << std::setprecision(2) << events_per_second
                      << std::setw(24) << average_nanoseconds << std::setprecision(6)
                      << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        print_usage(argv[0]);
        return 1;
    }
    return 0;
}
