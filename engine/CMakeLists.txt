cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER g++-12)
endif()

project(motorcycle_router LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

message(STATUS "motorcycle_router: CMake configured OK, C++${CMAKE_CXX_STANDARD}")

# Library
add_library(router_core
    src/instance_io.cpp
    src/split.cpp
    src/solution_io.cpp
    src/validate.cpp
    src/cost.cpp
    src/nearest_neighbour.cpp
    src/insertion.cpp
    src/solomon_i1.cpp
    src/local_search.cpp
)
target_include_directories(router_core PUBLIC include)
target_link_libraries(router_core PUBLIC nlohmann_json::nlohmann_json)


# App
add_executable(inspect_instance apps/inspect_instance.cpp)
target_link_libraries(inspect_instance PRIVATE router_core)

add_executable(validate_solution apps/validate_solution.cpp)
target_link_libraries(validate_solution PRIVATE router_core)

add_executable(compute_cost apps/compute_cost.cpp)
target_link_libraries(compute_cost PRIVATE router_core nlohmann_json::nlohmann_json)

add_executable(run_nn apps/run_nn.cpp)
target_link_libraries(run_nn PRIVATE router_core nlohmann_json::nlohmann_json)

add_executable(run_i1 apps/run_i1.cpp)
target_link_libraries(run_i1 PRIVATE router_core nlohmann_json::nlohmann_json)

add_executable(run_ls apps/run_ls.cpp)
target_link_libraries(run_ls PRIVATE router_core nlohmann_json::nlohmann_json)

option(WITH_ORTOOLS "Build the OR-Tools comparison bridge (run_ortools)" OFF)

if(WITH_ORTOOLS)
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/third_party/ortools")
    find_package(ortools CONFIG REQUIRED)

    add_executable(run_ortools apps/run_ortools.cpp)
    target_link_libraries(run_ortools PRIVATE router_core nlohmann_json::nlohmann_json ortools::ortools)
    target_link_options(run_ortools PRIVATE -fuse-ld=mold)
endif()