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

# Core Library
file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")
add_library(routing_core STATIC ${CORE_SOURCES})
target_link_libraries(routing_core PRIVATE nlohmann_json::nlohmann_json httplib::httplib)

# Create the engine executable
add_executable(routing_engine main.cpp)
target_link_libraries(routing_engine PRIVATE routing_core)

# Create the benchmark executable
add_executable(run_benchmark run_benchmark.cpp)
target_link_libraries(run_benchmark PRIVATE routing_core)

# Tests
enable_testing()
# (GTest or similar can be added here later)
