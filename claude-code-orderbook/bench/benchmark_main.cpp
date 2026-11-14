// Order book benchmark.
//
// For each (workload, size):
//   1. generate     - synthetic feed built up front          [timed separately: "gen ms"]
//   2. warm-up      - one untimed replay on a throwaway engine
//   3. throughput   - N repeats, fresh engine each           [engine build: "setup ms";
//                     only the replay loop is timed            replay: "replay ms"]
//   4. latency      - one more replay, rdtsc around every event, samples stored in a
//                     preallocated array; percentiles computed after the loop
//
// Inside timed loops: no logging, no I/O, no allocation by the harness (the trade
// scratch buffer is reserved up front). Allocation done *by the engine itself*
// (map/list/hash nodes) is part of what is being measured.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cycle_timer.hpp"
#include "latency_stats.hpp"
#include "orderbook/feed_generator.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/replay.hpp"

using namespace ob;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// Upper bound on trades from one event: a market/aggressive order of at most
// maxQty (100) against resting orders of at least 1 lot each. 4096 is ample,
// so the scratch buffer never reallocates inside the timed loop.
constexpr std::size_t kTradeScratch = 4096;

struct Options {
    std::vector<std::size_t> sizes{10'000, 100'000, 1'000'000};
    std::vector<std::string> workloads{"balanced", "cancel-heavy", "aggressive", "wide-book"};
    std::uint64_t seed = 42;
    int repeats = 5;
    std::string csvPath;
};

struct Result {
    std::string workload;
    std::size_t events = 0;
    double genMs = 0, setupMs = 0;
    std::vector<double> replayMs;
    double medianMs = 0, minMs = 0, maxMs = 0;
    double eventsPerSec = 0;
    bench::LatencySummary lat;
    ReplayStats stats;
    std::size_t finalOrders = 0, finalLevels = 0;
};

FeedConfig makeConfig(const std::string& name, std::size_t n, std::uint64_t seed) {
    if (name == "balanced") return FeedConfig::balanced(n, seed);
    if (name == "cancel-heavy") return FeedConfig::cancelHeavy(n, seed);
    if (name == "aggressive") return FeedConfig::aggressive(n, seed);
    if (name == "wide-book") return FeedConfig::wideBook(n, seed);
    throw std::invalid_argument("unknown workload: " + name);
}

// The exact loop that is timed for throughput. Kept tiny and inline.
ReplayStats replayLoop(MatchingEngine& eng, const std::vector<Event>& events, std::vector<Trade>& scratch) {
    ReplayStats st;
    for (const Event& ev : events) {
        applyEvent(eng, ev, scratch, st);
        scratch.clear();
    }
    return st;
}

Result runOne(const std::string& workload, std::size_t n, const Options& opt, double nsPerTick) {
    Result r;
    r.workload = workload;

    // 1. Generate (outside the measured replay).
    auto g0 = Clock::now();
    const std::vector<Event> events = generateFeed(makeConfig(workload, n, opt.seed));
    r.genMs = msSince(g0, Clock::now());
    r.events = events.size();

    std::vector<Trade> scratch;
    scratch.reserve(kTradeScratch);

    // 2. Warm-up.
    {
        MatchingEngine warm;
        warm.reserveOrders(n);
        r.stats = replayLoop(warm, events, scratch);
    }

    // 3. Throughput repeats.
    for (int rep = 0; rep < opt.repeats; ++rep) {
        auto s0 = Clock::now();
        auto eng = std::make_unique<MatchingEngine>();
        eng->reserveOrders(n);
        auto s1 = Clock::now();
        if (rep == 0) r.setupMs = msSince(s0, s1);

        auto t0 = Clock::now();
        const ReplayStats st = replayLoop(*eng, events, scratch);
        auto t1 = Clock::now();
        r.replayMs.push_back(msSince(t0, t1));

        if (!(st == r.stats)) {
            std::fprintf(stderr, "FATAL: non-deterministic replay (%s, n=%zu, repeat %d)\n",
                         workload.c_str(), n, rep);
            std::exit(2);
        }
        if (rep == 0) {
            r.finalOrders = eng->book().orderCount();
            r.finalLevels = eng->book().levelCount(Side::Buy) + eng->book().levelCount(Side::Sell);
        }
        eng.reset();   // teardown happens after t1: not part of the measurement
    }
    r.medianMs = bench::median(r.replayMs);
    r.minMs = *std::min_element(r.replayMs.begin(), r.replayMs.end());
    r.maxMs = *std::max_element(r.replayMs.begin(), r.replayMs.end());
    r.eventsPerSec = static_cast<double>(n) / (r.medianMs / 1000.0);

    // 4. Per-event latency.
    {
        std::vector<std::uint64_t> ticks(n);   // allocated and touched before timing
        auto eng = std::make_unique<MatchingEngine>();
        eng->reserveOrders(n);
        ReplayStats st;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t a = bench::CycleTimer::start();
            applyEvent(*eng, events[i], scratch, st);
            const std::uint64_t b = bench::CycleTimer::stop();
            ticks[i] = b - a;
            scratch.clear();
        }
        if (!(st == r.stats)) {
            std::fprintf(stderr, "FATAL: latency run diverged (%s, n=%zu)\n", workload.c_str(), n);
            std::exit(2);
        }
        eng.reset();
        r.lat = bench::summarize(ticks, nsPerTick);
    }
    return r;
}

bench::LatencySummary measureTimerOverhead(double nsPerTick) {
    constexpr std::size_t kSamples = 1'000'000;
    std::vector<std::uint64_t> ticks(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        const std::uint64_t a = bench::CycleTimer::start();
        const std::uint64_t b = bench::CycleTimer::stop();
        ticks[i] = b - a;
    }
    return bench::summarize(ticks, nsPerTick);
}

std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const auto comma = s.find(',', start);
        const auto end = comma == std::string_view::npos ? s.size() : comma;
        if (end > start) out.emplace_back(s.substr(start, end - start));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

void usage() {
    std::puts(
        "usage: orderbook_bench [--sizes N,N,...] [--workloads W,W,...] [--seed S]\n"
        "                       [--repeats R] [--csv results.csv]\n"
        "  workloads: balanced, cancel-heavy, aggressive, wide-book\n"
        "  defaults : --sizes 10000,100000,1000000 --seed 42 --repeats 5 (all workloads)");
}

Options parseArgs(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&]() -> std::string_view {
            if (i + 1 >= argc) { usage(); std::exit(1); }
            return argv[++i];
        };
        if (a == "--sizes") {
            opt.sizes.clear();
            for (const auto& s : splitList(next())) opt.sizes.push_back(std::stoull(s));
        } else if (a == "--workloads") {
            opt.workloads = splitList(next());
        } else if (a == "--seed") {
            opt.seed = std::stoull(std::string(next()));
        } else if (a == "--repeats") {
            opt.repeats = std::max(1, std::stoi(std::string(next())));
        } else if (a == "--csv") {
            opt.csvPath = std::string(next());
        } else {
            usage();
            std::exit(a == "--help" || a == "-h" ? 0 : 1);
        }
    }
    return opt;
}

void writeCsv(const std::string& path, const std::vector<Result>& results) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    out << "workload,events,gen_ms,setup_ms,replay_ms_median,replay_ms_min,replay_ms_max,events_per_sec,"
           "lat_avg_ns,lat_p50_ns,lat_p95_ns,lat_p99_ns,lat_p999_ns,lat_max_ns,trades,volume,"
           "cancels_requested,cancels_succeeded,final_orders,final_levels,checksum\n";
    for (const auto& r : results) {
        out << r.workload << ',' << r.events << ',' << r.genMs << ',' << r.setupMs << ',' << r.medianMs << ','
            << r.minMs << ',' << r.maxMs << ',' << r.eventsPerSec << ',' << r.lat.avg << ',' << r.lat.p50 << ','
            << r.lat.p95 << ',' << r.lat.p99 << ',' << r.lat.p999 << ',' << r.lat.max << ',' << r.stats.trades
            << ',' << r.stats.volume << ',' << r.stats.cancelsRequested << ',' << r.stats.cancelsSucceeded << ','
            << r.finalOrders << ',' << r.finalLevels << ',' << r.stats.checksum << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);
    for (const auto& w : opt.workloads) makeConfig(w, 1, 1);   // validate names early

    std::puts("=== Order book benchmark ===");
#ifdef NDEBUG
    std::puts("build      : Release (NDEBUG)");
#else
    std::puts("build      : *** DEBUG BUILD - numbers are NOT representative ***");
#endif
#ifdef _MSC_VER
    std::printf("compiler   : MSVC %d\n", _MSC_VER);
#elif defined(__clang__)
    std::printf("compiler   : clang %s\n", __clang_version__);
#elif defined(__GNUC__)
    std::printf("compiler   : gcc %s\n", __VERSION__);
#endif
    const double nsPerTick = bench::calibrateNsPerTick();
    std::printf("timer      : %s, %.4f ns/tick (%.3f GHz), invariant TSC: %s\n", bench::CycleTimer::kName,
                nsPerTick, 1.0 / nsPerTick, bench::hasInvariantTsc() ? "yes" : "NO");
    const auto ovh = measureTimerOverhead(nsPerTick);
    std::printf("timer cost : p50 %.1f ns, avg %.1f ns (included in every latency sample below)\n", ovh.p50,
                ovh.avg);
    std::printf("seed       : %llu, throughput repeats: %d (median reported)\n\n",
                static_cast<unsigned long long>(opt.seed), opt.repeats);

    std::vector<Result> results;
    for (const auto& w : opt.workloads) {
        for (std::size_t n : opt.sizes) {
            std::fprintf(stderr, "running %-13s n=%zu ...\n", w.c_str(), n);
            results.push_back(runOne(w, n, opt, nsPerTick));
        }
    }

    std::puts("--- Throughput (replay loop only; generation and setup excluded) ---");
    std::printf("%-13s %9s %9s %9s %11s %21s %12s %9s\n", "workload", "events", "gen ms", "setup ms",
                "replay ms", "[min .. max] ms", "events/s", "ns/event");
    for (const auto& r : results) {
        std::printf("%-13s %9zu %9.2f %9.3f %11.3f   [%7.3f .. %7.3f] %12.0f %9.1f\n", r.workload.c_str(),
                    r.events, r.genMs, r.setupMs, r.medianMs, r.minMs, r.maxMs, r.eventsPerSec,
                    r.medianMs * 1e6 / static_cast<double>(r.events));
    }

    std::puts("\n--- Per-event latency, ns (rdtsc around each event; includes timer cost) ---");
    std::printf("%-13s %9s %8s %8s %8s %8s %8s %10s\n", "workload", "events", "avg", "p50", "p95", "p99",
                "p99.9", "max");
    for (const auto& r : results) {
        std::printf("%-13s %9zu %8.1f %8.1f %8.1f %8.1f %8.1f %10.1f\n", r.workload.c_str(), r.events, r.lat.avg,
                    r.lat.p50, r.lat.p95, r.lat.p99, r.lat.p999, r.lat.max);
    }

    std::puts("\n--- Feed outcome (identical across all repeats) ---");
    std::printf("%-13s %9s %9s %11s %19s %10s %8s  %s\n", "workload", "events", "trades", "volume",
                "cancels ok/req", "end orders", "levels", "checksum");
    for (const auto& r : results) {
        std::printf("%-13s %9zu %9zu %11llu %9zu/%-9zu %10zu %8zu  %016llx\n", r.workload.c_str(), r.events,
                    r.stats.trades, static_cast<unsigned long long>(r.stats.volume), r.stats.cancelsSucceeded,
                    r.stats.cancelsRequested, r.finalOrders, r.finalLevels,
                    static_cast<unsigned long long>(r.stats.checksum));
    }

    if (!opt.csvPath.empty()) {
        writeCsv(opt.csvPath, results);
        std::printf("\nwrote %s\n", opt.csvPath.c_str());
    }
    return 0;
}
