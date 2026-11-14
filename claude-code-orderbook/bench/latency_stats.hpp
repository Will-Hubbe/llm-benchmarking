#pragma once
// Summary statistics over raw latency samples (in timer ticks).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bench {

struct LatencySummary {
    std::size_t count = 0;
    double min = 0, avg = 0, p50 = 0, p95 = 0, p99 = 0, p999 = 0, max = 0;   // nanoseconds
};

// Nearest-rank percentile on sorted data: the smallest sample such that at
// least p*100% of samples are <= it. p in (0, 1].
inline std::uint64_t percentileSorted(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(sorted.size())));
    rank = std::clamp<std::size_t>(rank, 1, sorted.size());
    return sorted[rank - 1];
}

// Sorts `samples` in place (call outside any timed region).
inline LatencySummary summarize(std::vector<std::uint64_t>& samples, double nsPerTick) {
    LatencySummary s;
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    long double sum = 0;
    for (auto v : samples) sum += v;
    const auto ns = [&](std::uint64_t ticks) { return static_cast<double>(ticks) * nsPerTick; };
    s.count = samples.size();
    s.min = ns(samples.front());
    s.max = ns(samples.back());
    s.avg = static_cast<double>(sum / static_cast<long double>(samples.size())) * nsPerTick;
    s.p50 = ns(percentileSorted(samples, 0.50));
    s.p95 = ns(percentileSorted(samples, 0.95));
    s.p99 = ns(percentileSorted(samples, 0.99));
    s.p999 = ns(percentileSorted(samples, 0.999));
    return s;
}

inline double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace bench
