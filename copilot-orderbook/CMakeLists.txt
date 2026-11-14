cmake_minimum_required(VERSION 3.20)
project(copilot_orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(orderbook
    src/orderbook.cpp
    src/feed_generator.cpp
)
target_include_directories(orderbook PUBLIC include)

add_executable(orderbook_demo src/main.cpp)
target_link_libraries(orderbook_demo PRIVATE orderbook)

add_executable(orderbook_tests tests/orderbook_tests.cpp)
target_link_libraries(orderbook_tests PRIVATE orderbook)

add_executable(orderbook_benchmark benchmarks/benchmark.cpp)
target_link_libraries(orderbook_benchmark PRIVATE orderbook)

enable_testing()
add_test(NAME orderbook_tests COMMAND orderbook_tests)
