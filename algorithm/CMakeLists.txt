cmake_minimum_required(VERSION 3.15)
project(MotorcycleRoutingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FetchContent for Dependencies
include(FetchContent)

# JSON for Modern C++
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json)

# HTTP Client (for OSRM)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.12.1
)
FetchContent_MakeAvailable(httplib)

# Include directories
include_directories(include)

# Google OR-Tools
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/ortools")
find_package(ortools REQUIRED CONFIG)

# Core Library
file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")
add_library(routing_core STATIC ${CORE_SOURCES})
target_link_libraries(routing_core PRIVATE nlohmann_json::nlohmann_json httplib::httplib ortools::ortools)

# Create the engine executable
add_executable(routing_engine main.cpp)
target_link_libraries(routing_engine PRIVATE routing_core)

# Create the benchmark executable
add_executable(run_benchmark run_benchmark.cpp)
target_link_libraries(run_benchmark PRIVATE routing_core)

# Tests
enable_testing()

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
add_executable(run_tests ${TEST_SOURCES})
target_link_libraries(run_tests PRIVATE routing_core GTest::gtest_main)
add_test(NAME AllTests COMMAND run_tests)

add_executable(verify_solver_integrity verify_solver_integrity.cpp)
target_link_libraries(verify_solver_integrity PRIVATE routing_core)
