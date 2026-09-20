#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ortools/constraint_solver/constraint_solver.h"
#include "ortools/constraint_solver/routing.h"
#include "ortools/constraint_solver/routing_enums.pb.h"
#include "ortools/constraint_solver/routing_index_manager.h"
#include "ortools/constraint_solver/routing_parameters.h"

#include "router/cost.hpp"
#include "router/instance_io.hpp"
#include "router/solution.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"
#include "router/zones.hpp"

using operations_research::Assignment;
using operations_research::DefaultRoutingSearchParameters;
using operations_research::FirstSolutionStrategy;
using operations_research::LocalSearchMetaheuristic;
using operations_research::RoutingDimension;
using operations_research::RoutingIndexManager;
using operations_research::RoutingModel;
using operations_research::RoutingSearchParameters;

namespace
{
    constexpr double kWeightScale = 100.0;
    constexpr double kVolumeScale = 10000.0;
}

int main(int argc, char **argv)
{
    bool rawMode = false;
    bool splitFlag = false;
    std::vector<std::string> positionalArgs;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--raw")
        {
            rawMode = true;
        }
        else if (arg == "--split")
        {
            splitFlag = true;
        }
        else if (arg.starts_with("--"))
        {
            std::cerr << "unknown option: " << arg << "\n";
            return 1;
        }
        else
        {
            positionalArgs.push_back(arg);
        }
    }

    if (rawMode && splitFlag)
    {
        std::cerr << "--raw and --split cannot be used together\n";
        return 1;
    }

    if (positionalArgs.empty() || positionalArgs.size() > 4)
    {
        std::cerr << "usage: run_ortools <instance.json> [time_limit_seconds] [num_zones] [zone_penalty] [--raw|--split]\n";
        return 1;
    }

    try
    {
        double timeLimitSeconds = 10.0;
        if (positionalArgs.size() >= 2)
        {
            std::size_t parsedChars = 0;
            timeLimitSeconds = std::stod(positionalArgs[1], &parsedChars);
            if (parsedChars != positionalArgs[1].size() ||
                !std::isfinite(timeLimitSeconds) ||
                timeLimitSeconds < 0.001 ||
                timeLimitSeconds > 86400.0)
            {
                throw std::invalid_argument(
                    "time_limit_seconds must be between 0.001 and 86400");
            }
        }
        const int64_t timeLimitMs = std::llround(timeLimitSeconds * 1000.0);
        const int numZones = positionalArgs.size() >= 3 ? std::stoi(positionalArgs[2]) : 0;
        const int64_t zonePenalty = positionalArgs.size() >= 4 ? std::stoll(positionalArgs[3]) : 100000;

        const router::Instance inst = router::loadInstance(positionalArgs[0]);
        std::vector<router::Visit> chunks;
        if (rawMode)
        {
            chunks.reserve(static_cast<std::size_t>(inst.n));
            for (int i = 1; i <= inst.n; ++i)
            {
                const router::Node &node = inst.nodes[static_cast<std::size_t>(i)];
                chunks.push_back({i, 0, 1, node.demandWeight, node.demandVolume});
            }
        }
        else
        {
            chunks = router::splitCustomers(inst);
        }
        const int numChunks = static_cast<int>(chunks.size());

        std::vector<int> origNode(static_cast<std::size_t>(numChunks) + 1);
        origNode[0] = 0;
        for (int i = 0; i < numChunks; ++i)
        {
            origNode[static_cast<std::size_t>(i) + 1] =
                chunks[static_cast<std::size_t>(i)].nodeIndex;
        }

        const RoutingIndexManager::NodeIndex depot(0);
        RoutingIndexManager manager(numChunks + 1, inst.fleet.size, depot);
        RoutingModel routing(manager);

        const int distanceCallback = routing.RegisterTransitCallback(
            [&](int64_t from, int64_t to) -> int64_t
            {
                const int a = origNode[static_cast<std::size_t>(manager.IndexToNode(from).value())];
                const int b = origNode[static_cast<std::size_t>(manager.IndexToNode(to).value())];
                return std::llround(inst.distanceMatrix[static_cast<std::size_t>(a)]
                                                       [static_cast<std::size_t>(b)]);
            });
        routing.SetArcCostEvaluatorOfAllVehicles(distanceCallback);

        const int weightCallback = routing.RegisterUnaryTransitCallback(
            [&](int64_t from) -> int64_t
            {
                const int node = manager.IndexToNode(from).value();
                if (node == 0)
                {
                    return 0;
                }
                return std::llround(chunks[static_cast<std::size_t>(node - 1)].weight * kWeightScale);
            });
        const std::vector<int64_t> weightCapacities(
            static_cast<std::size_t>(inst.fleet.size),
            static_cast<int64_t>(std::floor(inst.fleet.weightCapacity * kWeightScale)));
        routing.AddDimensionWithVehicleCapacity(weightCallback, int64_t{0}, weightCapacities, true, "Weight");

        const int volumeCallback = routing.RegisterUnaryTransitCallback(
            [&](int64_t from) -> int64_t
            {
                const int node = manager.IndexToNode(from).value();
                if (node == 0)
                {
                    return 0;
                }
                return std::llround(chunks[static_cast<std::size_t>(node - 1)].volume * kVolumeScale);
            });
        const std::vector<int64_t> volumeCapacities(
            static_cast<std::size_t>(inst.fleet.size),
            static_cast<int64_t>(std::floor(inst.fleet.volumeCapacity * kVolumeScale)));
        routing.AddDimensionWithVehicleCapacity(volumeCallback, int64_t{0}, volumeCapacities, true, "Volume");

        const int64_t horizon = static_cast<int64_t>(std::ceil(inst.horizon));
        const int timeCallback = routing.RegisterTransitCallback(
            [&](int64_t from, int64_t to) -> int64_t
            {
                const int a = origNode[static_cast<std::size_t>(manager.IndexToNode(from).value())];
                const int b = origNode[static_cast<std::size_t>(manager.IndexToNode(to).value())];
                const int64_t travel = static_cast<int64_t>(
                    std::ceil(inst.durationMatrix[static_cast<std::size_t>(a)]
                                                 [static_cast<std::size_t>(b)]));
                return travel + inst.nodes[static_cast<std::size_t>(a)].serviceTime;
            });
        routing.AddDimension(timeCallback, horizon, horizon, true, "Time");
        const RoutingDimension &timeDimension = routing.GetDimensionOrDie("Time");

        for (int i = 0; i < numChunks; ++i)
        {
            const router::Node &node =
                inst.nodes[static_cast<std::size_t>(chunks[static_cast<std::size_t>(i)].nodeIndex)];
            const int64_t index = manager.NodeToIndex(RoutingIndexManager::NodeIndex(i + 1));
            timeDimension.CumulVar(index)->SetRange(node.twStart, node.twEnd);
        }

        operations_research::Solver *const solver = routing.solver();
        std::map<int, std::vector<int>> chunksByCustomer;
        for (int i = 0; i < numChunks; ++i)
        {
            chunksByCustomer[chunks[static_cast<std::size_t>(i)].nodeIndex].push_back(i);
        }
        for (const auto &[customer, chunkIds] : chunksByCustomer)
        {
            for (std::size_t a = 0; a < chunkIds.size(); ++a)
            {
                for (std::size_t b = a + 1; b < chunkIds.size(); ++b)
                {
                    const int64_t indexA = manager.NodeToIndex(RoutingIndexManager::NodeIndex(chunkIds[a] + 1));
                    const int64_t indexB = manager.NodeToIndex(RoutingIndexManager::NodeIndex(chunkIds[b] + 1));
                    solver->AddConstraint(solver->MakeNonEquality(
                        routing.VehicleVar(indexA), routing.VehicleVar(indexB)));
                }
            }
        }

        std::vector<int> zoneOf;
        double zonePreprocessingMs = 0.0;
        if (numZones > 0)
        {
            const auto zoneStart = std::chrono::steady_clock::now();
            zoneOf = router::assignZones(inst, numZones);
            const auto zoneEnd = std::chrono::steady_clock::now();
            zonePreprocessingMs =
                std::chrono::duration<double, std::milli>(
                    zoneEnd - zoneStart).count();

            routing.AddRouteConstraint(
                [&](const std::vector<int64_t> &route)
                    -> std::optional<int64_t>
                {
                    std::unordered_set<int> zones;
                    for (const int64_t index : route)
                    {
                        const int node =
                            manager.IndexToNode(index).value();
                        if (node == 0)
                        {
                            continue;
                        }

                        const int customer =
                            origNode[static_cast<std::size_t>(node)];
                        zones.insert(
                            zoneOf[static_cast<std::size_t>(customer)]);
                    }

                    if (zones.empty())
                    {
                        return int64_t{0};
                    }

                    return zonePenalty *
                           static_cast<int64_t>(zones.size() - 1);
                },
                true);
        }

        RoutingSearchParameters searchParameters = DefaultRoutingSearchParameters();
        searchParameters.set_first_solution_strategy(FirstSolutionStrategy::PATH_CHEAPEST_ARC);
        searchParameters.set_local_search_metaheuristic(LocalSearchMetaheuristic::GUIDED_LOCAL_SEARCH);
        searchParameters.mutable_time_limit()->set_seconds(timeLimitMs / 1000);
        searchParameters.mutable_time_limit()->set_nanos(
            static_cast<int32_t>((timeLimitMs % 1000) * 1000000));

        const auto t0 = std::chrono::steady_clock::now();
        const Assignment *solution = routing.SolveWithParameters(searchParameters);
        const auto t1 = std::chrono::steady_clock::now();
        const double solveMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::ifstream instanceFile(positionalArgs[0]);
        nlohmann::json instanceJson;
        instanceFile >> instanceJson;
        const auto &meta = instanceJson.at("meta");

        nlohmann::json runResult = {
            {"schema_version", 1},
            {"instance", positionalArgs[0]},
            {"spatial_class", meta.value("spatial_class", "")},
            {"demand_class", meta.value("demand_class", "")},
            {"size", inst.n},
            {"seed", inst.seed},
            {"solver", "ortools"},
            {"mode", rawMode ? "RAW" : "SPLIT"},
            {"num_zones", numZones},
            {"zone_penalty", numZones > 0
                                 ? nlohmann::json(zonePenalty)
                                 : nlohmann::json(nullptr)},
            {"zone_objective", numZones > 0
                                  ? nlohmann::json("route_zone_excess")
                                  : nlohmann::json(nullptr)},
            {"zone_preprocessing_ms", numZones > 0
                                         ? nlohmann::json(zonePreprocessingMs)
                                         : nlohmann::json(nullptr)},
            {"solved", solution != nullptr},
            {"valid", nullptr},
            {"distance_cost", nullptr},
            {"route_count", nullptr},
            {"reference_cost", nullptr},
            {"reference_gap_pct", nullptr},
            {"zone_fragmentation", nullptr},
            {"route_zone_excess", nullptr},
            {"avg_zones_per_route", nullptr},
            {"route_zone_cost", nullptr},
            {"search_score", nullptr},
            {"runtime_ms", solveMs},
            {"runtime_scope", "search_only"},
            {"time_budget_ms", timeLimitMs},
            {"status", static_cast<int>(routing.status())},
            {"error", nullptr}};

        std::cout << "n              = " << inst.n << "\n";
        std::cout << "mode           = " << (rawMode ? "RAW" : "SPLIT") << "\n";
        std::cout << (rawMode ? "customers      = " : "chunks         = ") << numChunks << "\n";
        std::cout << "num_zones      = " << numZones << "\n";
        if (numZones > 0)
        {
            std::cout << "zone_penalty   = " << zonePenalty << "\n";
            std::cout << "zone prep time = "
                      << zonePreprocessingMs << " ms\n";
        }

        if (solution == nullptr)
        {
            std::cout << "OR-Tools       = NO SOLUTION FOUND (status "
                      << static_cast<int>(routing.status()) << ")\n";
            std::cout << "solve time     = " << solveMs << " ms\n";
            std::cout << "RESULT_JSON " << runResult.dump() << "\n";
            return 1;
        }

        router::Solution sol;
        for (int v = 0; v < inst.fleet.size; ++v)
        {
            if (!routing.IsVehicleUsed(*solution, v))
            {
                continue;
            }
            router::Route route;
            int64_t index = solution->Value(routing.NextVar(routing.Start(v)));
            while (!routing.IsEnd(index))
            {
                const int node = manager.IndexToNode(index).value();
                route.stops.push_back(chunks[static_cast<std::size_t>(node - 1)]);
                index = solution->Value(routing.NextVar(index));
            }
            if (!route.stops.empty())
            {
                sol.routes.push_back(std::move(route));
            }
        }

        const router::ValidationReport report = router::validate(inst, sol);
        const double cost = router::computeCost(inst, sol);

        runResult["valid"] = report.feasible;
        runResult["distance_cost"] = cost;
        runResult["route_count"] = sol.routes.size();

        std::cout << "routes used    = " << sol.routes.size() << " / " << inst.fleet.size << "\n";
        std::cout << "valid          = " << (report.feasible ? "YES" : "NO") << "\n";
        if (!report.feasible)
        {
            for (const auto &v : report.violations)
            {
                std::cout << "    - " << v << "\n";
            }
        }
        std::cout << "cost (OR-Tools)= " << cost << "\n";

        if (meta.contains("difficulty") &&
            meta["difficulty"].contains("reference_cost") &&
            meta["difficulty"]["reference_cost"].is_number())
        {
            const double referenceCost = meta["difficulty"]["reference_cost"].get<double>();
            if (std::isfinite(referenceCost) && referenceCost > 0.0)
            {
                const double gapPct = (cost - referenceCost) / referenceCost * 100.0;
                runResult["reference_cost"] = referenceCost;
                runResult["reference_gap_pct"] = gapPct;
                std::cout << "reference_cost = " << referenceCost << "\n";
                std::cout << "vs reference   = " << gapPct << " %\n";
            }
        }
        std::cout << "or-tools status= " << static_cast<int>(routing.status()) << "\n";
        std::cout << "solve time     = " << solveMs << " ms\n";

        if (numZones > 0)
        {
            const router::ZoneMetrics zoneMetrics =
                router::measureZoneCoherence(sol, zoneOf);
            const double routeZoneCost =
                router::computeRouteZoneCost(
                    sol, zoneOf, static_cast<double>(zonePenalty));

            std::cout << "zone fragment. = " << zoneMetrics.fragmentation
                      << " (sum over zones of extra vehicles used)\n";
            std::cout << "route zone excess= "
                      << zoneMetrics.routeZoneExcess << "\n";
            std::cout << "avg zones/route= "
                      << zoneMetrics.averageZonesPerRoute << "\n";
            std::cout << "route zone cost= "
                      << routeZoneCost << "\n";
            std::cout << "search score   = "
                      << (cost + routeZoneCost) << "\n";

            runResult["zone_fragmentation"] =
                zoneMetrics.fragmentation;
            runResult["route_zone_excess"] =
                zoneMetrics.routeZoneExcess;
            runResult["avg_zones_per_route"] =
                zoneMetrics.averageZonesPerRoute;
            runResult["route_zone_cost"] = routeZoneCost;
            runResult["search_score"] = cost + routeZoneCost;
        }

        std::cout << "RESULT_JSON " << runResult.dump() << "\n";
        return report.feasible ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}