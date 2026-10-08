cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER g++-12)
endif()

project(motorcycle_router LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
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
    src/zones.cpp
    src/nearest_neighbour.cpp
    src/insertion.cpp
    src/solomon_i1.cpp
    src/local_search.cpp
    src/hgs.cpp
    src/hgs/problem_data.cpp
    src/hgs/cost_model.cpp
    src/hgs/search_context.cpp
    src/hgs/individual.cpp
    src/hgs/split.cpp
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

add_executable(inspect_zones apps/inspect_zones.cpp)
target_link_libraries(inspect_zones PRIVATE router_core nlohmann_json::nlohmann_json)

add_executable(run_nn apps/run_nn.cpp)
target_link_libraries(run_nn PRIVATE router_core nlohmann_json::nlohmann_json)

add_executable(run_i1 apps/run_i1.cpp)
target_link_libraries(run_i1 PRIVATE router_core nlohmann_json::nlohmann_json)

add_executable(run_ls apps/run_ls.cpp)
target_link_libraries(run_ls PRIVATE router_core nlohmann_json::nlohmann_json)

add_executable(run_hgs apps/run_hgs.cpp)
target_link_libraries(run_hgs PRIVATE router_core nlohmann_json::nlohmann_json)

add_executable(bench_hgs_split apps/bench_hgs_split.cpp)
target_link_libraries(bench_hgs_split PRIVATE router_core)

add_executable(bench_split_v2 apps/bench_split_v2.cpp)
target_link_libraries(bench_split_v2 PRIVATE router_core)

option(WITH_ORTOOLS "Build the OR-Tools comparison bridge (run_ortools)" OFF)

if(WITH_ORTOOLS)
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/third_party/ortools")
    find_package(ortools CONFIG REQUIRED)

    add_executable(run_ortools apps/run_ortools.cpp)
    target_link_libraries(run_ortools PRIVATE router_core nlohmann_json::nlohmann_json ortools::ortools)
    target_link_options(run_ortools PRIVATE -fuse-ld=mold)
endif()

if(BUILD_TESTING)
    add_executable(test_zones tests/test_zones.cpp)
    target_link_libraries(test_zones PRIVATE router_core)

    add_executable(test_local_search tests/test_local_search.cpp)
    target_link_libraries(test_local_search PRIVATE router_core)

    add_executable(test_hgs tests/test_hgs.cpp)
    target_link_libraries(test_hgs PRIVATE router_core)

    add_test(NAME zones COMMAND test_zones)
    add_test(
        NAME local_search
        COMMAND test_local_search
            "${PROJECT_SOURCE_DIR}/../data/instances/C_D1_n20_loose_s42_instance.json"
            "${PROJECT_SOURCE_DIR}/../data/instances/R_D1_n100_tight_s42_instance.json"
    )
    add_test(NAME hgs COMMAND test_hgs)

    foreach(hgs_test_name problem_data route_summary search_context individual split)
        add_executable(test_hgs_${hgs_test_name} tests/hgs/test_${hgs_test_name}.cpp)
        target_include_directories(test_hgs_${hgs_test_name} PRIVATE tests)
        target_link_libraries(test_hgs_${hgs_test_name} PRIVATE router_core)
        add_test(NAME hgs_${hgs_test_name} COMMAND test_hgs_${hgs_test_name})
    endforeach()
endif()
