// The benchmark's statistics code gets its own tests: a wrong percentile
// would silently make every reported number wrong.
#include <cstdint>
#include <vector>

#include "latency_stats.hpp"
#include "test_framework.hpp"

TEST_CASE("stats: nearest-rank percentiles on 1..100") {
    std::vector<std::uint64_t> v;
    for (std::uint64_t i = 100; i >= 1; --i) v.push_back(i);   // unsorted input
    auto s = bench::summarize(v, 1.0);
    CHECK_EQ(s.count, 100);
    CHECK_EQ(s.min, 1.0);
    CHECK_EQ(s.max, 100.0);
    CHECK_EQ(s.avg, 50.5);
    CHECK_EQ(s.p50, 50.0);
    CHECK_EQ(s.p95, 95.0);
    CHECK_EQ(s.p99, 99.0);
    CHECK_EQ(s.p999, 100.0);
}

TEST_CASE("stats: tail percentiles catch a single outlier") {
    std::vector<std::uint64_t> v(1000, 10);
    v[500] = 5000;
    auto s = bench::summarize(v, 1.0);
    CHECK_EQ(s.p50, 10.0);
    CHECK_EQ(s.p99, 10.0);
    CHECK_EQ(s.p999, 10.0);   // 999/1000 samples are 10
    CHECK_EQ(s.max, 5000.0);
}

TEST_CASE("stats: tick-to-ns scaling, single sample, empty input") {
    std::vector<std::uint64_t> v{40};
    auto s = bench::summarize(v, 0.25);
    CHECK_EQ(s.p50, 10.0);
    CHECK_EQ(s.p99, 10.0);
    std::vector<std::uint64_t> empty;
    CHECK_EQ(bench::summarize(empty, 1.0).count, 0);
}

TEST_CASE("stats: median of odd and even counts") {
    CHECK_EQ(bench::median({3.0, 1.0, 2.0}), 2.0);
    CHECK_EQ(bench::median({4.0, 1.0, 3.0, 2.0}), 2.5);
}
