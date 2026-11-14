// Bottleneck profiler (separate binary so its instrumentation never touches
// orderbook_bench numbers).
//
//  * Classifies every event (rest at existing level, rest at new level,
//    crossing limit, market, cancel hit, cancel miss) and reports latency and
//    share of total time per class.
//  * Replaces global operator new in THIS executable to count heap
//    allocations per event and record allocation sizes (= real node sizes).
//  * Counts page faults during the replay (Windows).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "cycle_timer.hpp"
#include "latency_stats.hpp"
#include "orderbook/feed_generator.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/replay.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

// ---- Allocation hooks (single-threaded program; plain counters) -------------
namespace {
std::uint64_t g_allocs = 0;
std::uint64_t g_bytes = 0;
std::array<std::uint64_t, 513> g_sizeHist{};   // [512] = "larger"
}  // namespace

void* operator new(std::size_t n) {
    ++g_allocs;
    g_bytes += n;
    ++g_sizeHist[n < 512 ? n : 512];
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace ob;

namespace {

enum Cat : std::uint8_t { RestExisting, RestNewLevel, LimitCross, Market, CancelHit, CancelMiss, kNumCats };
constexpr const char* kCatName[kNumCats] = {"limit: rest, existing lvl", "limit: rest, NEW level",
                                            "limit: crosses (trades)",   "market order",
                                            "cancel: hit",               "cancel: miss"};

std::uint64_t pageFaults() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return pmc.PageFaultCount;
#endif
    return 0;
}

FeedConfig makeConfig(const std::string& name, std::size_t n) {
    if (name == "balanced") return FeedConfig::balanced(n);
    if (name == "cancel-heavy") return FeedConfig::cancelHeavy(n);
    if (name == "aggressive") return FeedConfig::aggressive(n);
    return FeedConfig::wideBook(n);
}

void profile(const std::string& workload, std::size_t n, double nsPerTick) {
    const auto events = generateFeed(makeConfig(workload, n));

    // Everything the loop writes is preallocated here.
    std::vector<std::uint64_t> ticks(n);
    std::vector<std::uint8_t> cat(n);
    std::vector<std::uint16_t> allocs(n);
    std::vector<Trade> scratch;
    scratch.reserve(4096);
    auto eng = std::make_unique<MatchingEngine>();
    eng->reserveOrders(n);
    ReplayStats st;

    g_sizeHist.fill(0);
    const std::uint64_t allocs0 = g_allocs, bytes0 = g_bytes, pf0 = pageFaults();

    for (std::size_t i = 0; i < n; ++i) {
        const Event& ev = events[i];
        const std::size_t lvlBefore = eng->book().levelCount(ev.order.side);
        const std::size_t cancelsOkBefore = st.cancelsSucceeded;
        const std::uint64_t a0 = g_allocs;

        const std::uint64_t t0 = bench::CycleTimer::start();
        applyEvent(*eng, ev, scratch, st);
        const std::uint64_t t1 = bench::CycleTimer::stop();

        ticks[i] = t1 - t0;
        allocs[i] = static_cast<std::uint16_t>(std::min<std::uint64_t>(g_allocs - a0, 65535));
        if (ev.type == EventType::Cancel)
            cat[i] = st.cancelsSucceeded > cancelsOkBefore ? CancelHit : CancelMiss;
        else if (ev.order.type == OrderType::Market)
            cat[i] = Market;
        else if (!scratch.empty())
            cat[i] = LimitCross;
        else
            cat[i] = eng->book().levelCount(ev.order.side) > lvlBefore ? RestNewLevel : RestExisting;
        scratch.clear();
    }

    const std::uint64_t pf = pageFaults() - pf0;
    const std::uint64_t totAllocs = g_allocs - allocs0, totBytes = g_bytes - bytes0;
    const auto histCopy = g_sizeHist;
    const std::size_t endOrders = eng->book().orderCount();
    const std::size_t endLevels = eng->book().levelCount(Side::Buy) + eng->book().levelCount(Side::Sell);

    // ---- Aggregate (after the loop) ----
    std::uint64_t totalTicks = 0;
    for (auto t : ticks) totalTicks += t;

    std::printf("\n=== %s, %zu events  (end book: %zu orders, %zu levels) ===\n", workload.c_str(), n, endOrders,
                endLevels);
    std::printf("%-26s %8s %7s %8s %8s %8s %8s %11s\n", "event class", "count", "share", "time %", "avg ns",
                "p50 ns", "p99 ns", "allocs/evt");
    for (int c = 0; c < kNumCats; ++c) {
        std::vector<std::uint64_t> s;
        std::uint64_t sumT = 0, sumA = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (cat[i] != c) continue;
            s.push_back(ticks[i]);
            sumT += ticks[i];
            sumA += allocs[i];
        }
        if (s.empty()) continue;
        const auto sum = bench::summarize(s, nsPerTick);
        std::printf("%-26s %8zu %6.1f%% %7.1f%% %8.1f %8.1f %8.1f %11.2f\n", kCatName[c], s.size(),
                    100.0 * static_cast<double>(s.size()) / static_cast<double>(n),
                    100.0 * static_cast<double>(sumT) / static_cast<double>(totalTicks), sum.avg, sum.p50, sum.p99,
                    static_cast<double>(sumA) / static_cast<double>(s.size()));
    }
    std::printf("heap: %.2f allocations/event, %.1f bytes/event; page faults: %llu (%.1f per 1k events)\n",
                static_cast<double>(totAllocs) / static_cast<double>(n),
                static_cast<double>(totBytes) / static_cast<double>(n), static_cast<unsigned long long>(pf),
                1000.0 * static_cast<double>(pf) / static_cast<double>(n));
    std::printf("allocation sizes:");
    for (std::size_t sz = 0; sz < histCopy.size(); ++sz)
        if (histCopy[sz]) std::printf("  %s%zuB x %llu", sz == 512 ? ">=" : "", sz,
                                      static_cast<unsigned long long>(histCopy[sz]));
    std::printf("\n");

    // Where do the slowest 0.1% of events come from?
    std::vector<std::uint64_t> sorted = ticks;
    std::sort(sorted.begin(), sorted.end());
    const std::uint64_t cut = bench::percentileSorted(sorted, 0.999);
    std::array<std::size_t, kNumCats> tailByCat{};
    std::size_t tailN = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (ticks[i] > cut) { ++tailByCat[cat[i]]; ++tailN; }
    std::printf("slowest 0.1%% (> %.0f ns, %zu events) by class:", static_cast<double>(cut) * nsPerTick, tailN);
    for (int c = 0; c < kNumCats; ++c)
        if (tailByCat[c]) std::printf("  %s=%zu", kCatName[c], tailByCat[c]);
    std::printf("\n");

}

// Reference cost of the default heap for engine-sized nodes.
void mallocReference(double nsPerTick) {
    constexpr std::size_t kN = 1'000'000;
    std::vector<void*> ptrs(kN);
    const auto t0 = bench::CycleTimer::start();
    for (std::size_t i = 0; i < kN; ++i) ptrs[i] = std::malloc(64);
    const auto t1 = bench::CycleTimer::stop();
    for (std::size_t i = 0; i < kN; ++i) std::free(ptrs[i]);
    const auto t2 = bench::CycleTimer::stop();
    std::printf("\nreference: malloc(64) %.1f ns, free %.1f ns (1M back-to-back, fresh heap)\n",
                static_cast<double>(t1 - t0) * nsPerTick / kN, static_cast<double>(t2 - t1) * nsPerTick / kN);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::size_t> sizes{100'000, 1'000'000};
    if (argc > 1) sizes = {static_cast<std::size_t>(std::stoull(argv[1]))};
    const double nsPerTick = bench::calibrateNsPerTick();
    std::printf("profile build: %s | timer %.4f ns/tick | sizeof(Order)=%zu sizeof(Trade)=%zu\n",
#ifdef NDEBUG
                "Release",
#else
                "DEBUG (not representative)",
#endif
                nsPerTick, sizeof(Order), sizeof(Trade));
    for (const char* w : {"balanced", "cancel-heavy", "aggressive", "wide-book"})
        for (auto n : sizes) profile(w, n, nsPerTick);
    mallocReference(nsPerTick);
    return 0;
}
