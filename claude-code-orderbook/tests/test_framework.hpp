#pragma once
// Minimal test harness. TEST_CASE registers a function; CHECK/CHECK_EQ record a
// failure and keep going; REQUIRE aborts the current test. Run with an optional
// substring filter:  unit_tests [filter]
#include <concepts>
#include <cstdio>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tl {
struct TestCase { const char* name; void (*fn)(); };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline std::vector<std::string>& failures() { static std::vector<std::string> f; return f; }
struct Registrar { Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); } };
struct AbortTest {};

template <class T> std::string show(const T& v) {
    if constexpr (requires(std::ostream& os) { os << v; }) { std::ostringstream os; os << v; return os.str(); }
    else if constexpr (requires { v.has_value(); *v; }) { return v ? show(*v) : std::string("nullopt"); }
    else if constexpr (requires { v.begin(); v.end(); }) {
        std::string s = "{";
        for (const auto& e : v) s += "\n        " + show(e);
        return s + " }";
    } else return "<unprintable>";
}
inline void fail(const char* file, int line, const std::string& msg) {
    failures().push_back(std::string(file) + ":" + std::to_string(line) + ": " + msg);
}
template <class A, class B> bool equal(const A& a, const B& b) {
    if constexpr (std::integral<A> && std::integral<B> && !std::same_as<A, bool> && !std::same_as<B, bool>)
        return std::cmp_equal(a, b);   // avoids signed/unsigned surprises
    else return a == b;
}
template <class A, class B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    if (!equal(a, b)) fail(file, line, std::string("CHECK_EQ(") + ea + ", " + eb + ")\n      lhs: " + show(a) + "\n      rhs: " + show(b));
}
inline int runAll(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    for (const auto& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        failures().clear();
        try { t.fn(); }
        catch (const AbortTest&) {}
        catch (const std::exception& e) { fail("?", 0, std::string("uncaught exception: ") + e.what()); }
        catch (...) { fail("?", 0, "uncaught unknown exception"); }
        if (failures().empty()) { ++passed; std::printf("[ OK ] %s\n", t.name); continue; }
        ++failed; std::printf("[FAIL] %s\n", t.name);
        for (const auto& f : failures()) std::printf("    %s\n", f.c_str());
    }
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
}  // namespace tl

#define TL_CAT2(a, b) a##b
#define TL_CAT(a, b) TL_CAT2(a, b)
#define TEST_CASE(name)                                                                 \
    static void TL_CAT(tl_test_, __LINE__)();                                           \
    static ::tl::Registrar TL_CAT(tl_reg_, __LINE__)(name, &TL_CAT(tl_test_, __LINE__)); \
    static void TL_CAT(tl_test_, __LINE__)()
#define CHECK(c) ((c) ? (void)0 : ::tl::fail(__FILE__, __LINE__, "CHECK(" #c ")"))
#define CHECK_EQ(a, b) ::tl::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
#define REQUIRE(c) do { if (!(c)) { ::tl::fail(__FILE__, __LINE__, "REQUIRE(" #c ")"); throw ::tl::AbortTest{}; } } while (0)
