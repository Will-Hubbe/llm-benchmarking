cmake_minimum_required(VERSION 3.20)
project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators (Ninja/Makefiles) default to Release.
# Multi-config generators (Visual Studio) choose with --config.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Warning flags shared by every target.
add_library(ob_warnings INTERFACE)
if(MSVC)
    target_compile_options(ob_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(ob_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---- Core library -----------------------------------------------------------
add_library(orderbook_core STATIC
    src/order_book.cpp
    src/matching_engine.cpp
    src/feed_generator.cpp
    src/replay.cpp
)
target_include_directories(orderbook_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(orderbook_core PUBLIC ob_warnings)

# ---- Unit tests -------------------------------------------------------------
enable_testing()
add_executable(unit_tests
    tests/test_main.cpp
    tests/test_order_book.cpp
    tests/test_matching.cpp
    tests/test_cancel_edge.cpp
    tests/test_reference_model.cpp
    tests/test_generator_replay.cpp
    tests/test_latency_stats.cpp
)
target_link_libraries(unit_tests PRIVATE orderbook_core)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bench)
# CSV tests write their files here (inside build/, never outside the project).
target_compile_definitions(unit_tests PRIVATE OB_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_output")
add_test(NAME unit_tests COMMAND unit_tests)

# ---- Benchmarks (measure with: cmake --build build --config Release) --------
add_executable(orderbook_bench bench/benchmark_main.cpp)
target_link_libraries(orderbook_bench PRIVATE orderbook_core)

# Bottleneck profiler: per-event-class latency, allocation counts, page faults.
# Separate binary because it replaces global operator new.
add_executable(orderbook_profile bench/profile_main.cpp)
target_link_libraries(orderbook_profile PRIVATE orderbook_core)
if(WIN32)
    target_link_libraries(orderbook_profile PRIVATE psapi)
endif()
