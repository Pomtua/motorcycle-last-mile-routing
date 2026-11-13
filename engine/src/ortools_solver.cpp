#include "router/ortools_solver.hpp"

#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_set>

#include "ortools/constraint_solver/constraint_solver.h"
#include "ortools/constraint_solver/routing.h"
#include "ortools/constraint_solver/routing_enums.pb.h"
#include "ortools/constraint_solver/routing_index_manager.h"
#include "ortools/constraint_solver/routing_parameters.h"

using operations_research::Assignment;
using operations_research::DefaultRoutingSearchParameters;
using operations_research::FirstSolutionStrategy;
using operations_research::LocalSearchMetaheuristic;
using operations_research::RoutingDimension;
using operations_research::RoutingIndexManager;
using operations_research::RoutingModel;
using operations_research::RoutingSearchParameters;

namespace router
{
    namespace
    {
        constexpr double kWeightScale = 100.0;
        constexpr double kVolumeScale = 10000.0;
        constexpr double kTimeScale = 10.0;
        constexpr double kScaleRoundingSlack = 1e-6;
    }

    OrToolsResult solveWithOrTools(
        const Instance &inst,
        const std::vector<Visit> &chunks,
        double timeLimitSeconds,
        const std::vector<int> &zoneOf,
        std::int64_t zonePenalty)
    {
        if (!std::isfinite(timeLimitSeconds) || timeLimitSeconds < 1e-9 || timeLimitSeconds > 86400.0)
        {
            throw std::invalid_argument("time_limit_seconds must be between 1e-9 and 86400");
        }
        const std::int64_t timeLimitNs = std::llround(timeLimitSeconds * 1000000000.0);
        const int numChunks = static_cast<int>(chunks.size());

        std::vector<int> origNode(static_cast<std::size_t>(numChunks) + 1);
        origNode[0] = 0;
        for (int i = 0; i < numChunks; ++i)
        {
            origNode[static_cast<std::size_t>(i) + 1] = chunks[static_cast<std::size_t>(i)].nodeIndex;
        }

        const RoutingIndexManager::NodeIndex depot(0);
        RoutingIndexManager manager(numChunks + 1, inst.fleet.size, depot);
        RoutingModel routing(manager);

        const int distanceCallback = routing.RegisterTransitCallback(
            [&](std::int64_t from, std::int64_t to) -> std::int64_t
            {
                const int a = origNode[static_cast<std::size_t>(manager.IndexToNode(from).value())];
                const int b = origNode[static_cast<std::size_t>(manager.IndexToNode(to).value())];
                return std::llround(inst.distanceMatrix[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)]);
            });
        routing.SetArcCostEvaluatorOfAllVehicles(distanceCallback);

        const int weightCallback = routing.RegisterUnaryTransitCallback(
            [&](std::int64_t from) -> std::int64_t
            {
                const int node = manager.IndexToNode(from).value();
                if (node == 0)
                {
                    return 0;
                }
                return std::llround(chunks[static_cast<std::size_t>(node - 1)].weight * kWeightScale);
            });
        const std::vector<std::int64_t> weightCapacities(
            static_cast<std::size_t>(inst.fleet.size),
            static_cast<std::int64_t>(std::floor(inst.fleet.weightCapacity * kWeightScale)));
        routing.AddDimensionWithVehicleCapacity(weightCallback, std::int64_t{0}, weightCapacities, true, "Weight");

        const int volumeCallback = routing.RegisterUnaryTransitCallback(
            [&](std::int64_t from) -> std::int64_t
            {
                const int node = manager.IndexToNode(from).value();
                if (node == 0)
                {
                    return 0;
                }
                return static_cast<std::int64_t>(
                    std::ceil(chunks[static_cast<std::size_t>(node - 1)].volume * kVolumeScale - kScaleRoundingSlack));
            });
        const std::vector<std::int64_t> volumeCapacities(
            static_cast<std::size_t>(inst.fleet.size),
            static_cast<std::int64_t>(std::floor(inst.fleet.volumeCapacity * kVolumeScale)));
        routing.AddDimensionWithVehicleCapacity(volumeCallback, std::int64_t{0}, volumeCapacities, true, "Volume");

        const auto timeUp = [](double seconds)
        { return static_cast<std::int64_t>(std::ceil(seconds * kTimeScale - kScaleRoundingSlack)); };
        const auto timeDown = [](double seconds)
        { return static_cast<std::int64_t>(std::floor(seconds * kTimeScale + kScaleRoundingSlack)); };

        const std::int64_t horizon = timeDown(inst.horizon);
        const int timeCallback = routing.RegisterTransitCallback(
            [&](std::int64_t from, std::int64_t to) -> std::int64_t
            {
                const int a = origNode[static_cast<std::size_t>(manager.IndexToNode(from).value())];
                const int b = origNode[static_cast<std::size_t>(manager.IndexToNode(to).value())];
                return timeUp(inst.durationMatrix[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] +
                              inst.nodes[static_cast<std::size_t>(a)].serviceTime);
            });
        routing.AddDimension(timeCallback, horizon, horizon, true, "Time");
        const RoutingDimension &timeDimension = routing.GetDimensionOrDie("Time");

        for (int i = 0; i < numChunks; ++i)
        {
            const Node &node = inst.nodes[static_cast<std::size_t>(chunks[static_cast<std::size_t>(i)].nodeIndex)];
            const std::int64_t index = manager.NodeToIndex(RoutingIndexManager::NodeIndex(i + 1));
            timeDimension.CumulVar(index)->SetRange(timeUp(node.twStart), timeDown(node.twEnd));
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
                    const std::int64_t indexA = manager.NodeToIndex(RoutingIndexManager::NodeIndex(chunkIds[a] + 1));
                    const std::int64_t indexB = manager.NodeToIndex(RoutingIndexManager::NodeIndex(chunkIds[b] + 1));
                    solver->AddConstraint(solver->MakeNonEquality(routing.VehicleVar(indexA), routing.VehicleVar(indexB)));
                }
            }
        }

        if (!zoneOf.empty())
        {
            routing.AddRouteConstraint(
                [&](const std::vector<std::int64_t> &route) -> std::optional<std::int64_t>
                {
                    std::unordered_set<int> zones;
                    for (const std::int64_t index : route)
                    {
                        const int node = manager.IndexToNode(index).value();
                        if (node == 0)
                        {
                            continue;
                        }
                        const int customer = origNode[static_cast<std::size_t>(node)];
                        zones.insert(zoneOf[static_cast<std::size_t>(customer)]);
                    }
                    if (zones.empty())
                    {
                        return std::int64_t{0};
                    }
                    return zonePenalty * static_cast<std::int64_t>(zones.size() - 1);
                },
                true);
        }

        RoutingSearchParameters searchParameters = DefaultRoutingSearchParameters();
        searchParameters.set_first_solution_strategy(FirstSolutionStrategy::PATH_CHEAPEST_ARC);
        searchParameters.set_local_search_metaheuristic(LocalSearchMetaheuristic::GUIDED_LOCAL_SEARCH);
        searchParameters.mutable_time_limit()->set_seconds(timeLimitNs / 1000000000);
        searchParameters.mutable_time_limit()->set_nanos(static_cast<std::int32_t>(timeLimitNs % 1000000000));

        const auto t0 = std::chrono::steady_clock::now();
        const Assignment *assignment = routing.SolveWithParameters(searchParameters);
        const auto t1 = std::chrono::steady_clock::now();

        OrToolsResult result;
        result.solveMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        result.status = static_cast<int>(routing.status());
        result.solved = assignment != nullptr;
        if (!result.solved)
        {
            return result;
        }

        for (int v = 0; v < inst.fleet.size; ++v)
        {
            if (!routing.IsVehicleUsed(*assignment, v))
            {
                continue;
            }
            Route route;
            std::int64_t index = assignment->Value(routing.NextVar(routing.Start(v)));
            while (!routing.IsEnd(index))
            {
                const int node = manager.IndexToNode(index).value();
                route.stops.push_back(chunks[static_cast<std::size_t>(node - 1)]);
                index = assignment->Value(routing.NextVar(index));
            }
            if (!route.stops.empty())
            {
                result.solution.routes.push_back(std::move(route));
            }
        }
        return result;
    }
}
