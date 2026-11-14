#pragma once
// High-resolution per-event timer.
//
// On x86 we read the CPU timestamp counter. lfence/rdtscp keep the reads from
// being reordered around the measured code. Ticks are converted to ns using a
// calibration against steady_clock. Elsewhere we fall back to steady_clock.
#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#define OB_HAVE_TSC 1
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <x86intrin.h>
#define OB_HAVE_TSC 1
#else
#define OB_HAVE_TSC 0
#endif

namespace bench {

struct CycleTimer {
#if OB_HAVE_TSC
    static constexpr const char* kName = "rdtsc";
    static inline std::uint64_t start() noexcept {
        _mm_lfence();
        const std::uint64_t t = __rdtsc();
        _mm_lfence();
        return t;
    }
    static inline std::uint64_t stop() noexcept {
        unsigned aux;
        const std::uint64_t t = __rdtscp(&aux);   // waits for prior instructions
        _mm_lfence();
        return t;
    }
#else
    static constexpr const char* kName = "steady_clock";
    static inline std::uint64_t now() noexcept {
        return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    static inline std::uint64_t start() noexcept { return now(); }
    static inline std::uint64_t stop() noexcept { return now(); }
#endif
};

// Does the CPU advertise an invariant TSC (constant rate across P-states)?
inline bool hasInvariantTsc() {
#if OB_HAVE_TSC
#if defined(_MSC_VER)
    int r[4];
    __cpuid(r, 0x80000000);
    if (static_cast<unsigned>(r[0]) < 0x80000007u) return false;
    __cpuid(r, 0x80000007);
    return (r[3] & (1 << 8)) != 0;
#else
    unsigned a, b, c, d;
    if (!__get_cpuid(0x80000007, &a, &b, &c, &d)) return false;
    return (d & (1u << 8)) != 0;
#endif
#else
    return false;
#endif
}

// Nanoseconds per CycleTimer tick, measured against steady_clock over ~`ms`.
inline double calibrateNsPerTick(int ms = 250) {
#if OB_HAVE_TSC
    using clk = std::chrono::steady_clock;
    const auto w0 = clk::now();
    const auto c0 = CycleTimer::start();
    while (clk::now() - w0 < std::chrono::milliseconds(ms)) { /* spin */ }
    const auto c1 = CycleTimer::stop();
    const auto w1 = clk::now();
    const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(w1 - w0).count());
    return ns / static_cast<double>(c1 - c0);
#else
    (void)ms;
    using period = std::chrono::steady_clock::period;
    return 1e9 * static_cast<double>(period::num) / static_cast<double>(period::den);
#endif
}

}  // namespace bench
