#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <set>
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

    struct Point
    {
        double x = 0.0;
        double y = 0.0;
    };

    std::vector<int> kMeansZones(const router::Instance &inst, int k)
    {
        const std::size_t n = inst.nodes.size();
        double latSum = 0.0;
        for (std::size_t i = 1; i < n; ++i)
        {
            latSum += inst.nodes[i].lat;
        }
        const double avgLat = latSum / static_cast<double>(n - 1);
        const double lonScale = std::cos(avgLat * M_PI / 180.0);

        std::vector<Point> pts(n);
        for (std::size_t i = 1; i < n; ++i)
        {
            pts[i] = {inst.nodes[i].lng * lonScale, inst.nodes[i].lat};
        }

        std::mt19937 rng(static_cast<unsigned>(inst.seed));
        std::vector<int> pool(n - 1);
        std::iota(pool.begin(), pool.end(), 1);
        std::shuffle(pool.begin(), pool.end(), rng);

        std::vector<Point> centroids(static_cast<std::size_t>(k));
        for (int c = 0; c < k; ++c)
        {
            centroids[static_cast<std::size_t>(c)] = pts[pool[static_cast<std::size_t>(c) % pool.size()]];
        }

        std::vector<int> assign(n, -1);
        for (int iter = 0; iter < 25; ++iter)
        {
            for (std::size_t i = 1; i < n; ++i)
            {
                double best = 1e18;
                int bestC = 0;
                for (int c = 0; c < k; ++c)
                {
                    const double dx = pts[i].x - centroids[static_cast<std::size_t>(c)].x;
                    const double dy = pts[i].y - centroids[static_cast<std::size_t>(c)].y;
                    const double d = dx * dx + dy * dy;
                    if (d < best)
                    {
                        best = d;
                        bestC = c;
                    }
                }
                assign[i] = bestC;
            }

            std::vector<Point> sum(static_cast<std::size_t>(k));
            std::vector<int> count(static_cast<std::size_t>(k), 0);
            for (std::size_t i = 1; i < n; ++i)
            {
                const std::size_t c = static_cast<std::size_t>(assign[i]);
                sum[c].x += pts[i].x;
                sum[c].y += pts[i].y;
                ++count[c];
            }
            for (int c = 0; c < k; ++c)
            {
                if (count[static_cast<std::size_t>(c)] > 0)
                {
                    centroids[static_cast<std::size_t>(c)] = {
                        sum[static_cast<std::size_t>(c)].x / count[static_cast<std::size_t>(c)],
                        sum[static_cast<std::size_t>(c)].y / count[static_cast<std::size_t>(c)]};
                }
            }
        }
        return assign;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 5)
    {
        std::cerr << "usage: run_ortools <instance.json> [time_limit_seconds] [num_zones] [zone_penalty]\n";
        return 1;
    }

    const int64_t timeLimitSeconds = argc >= 3 ? std::stoll(argv[2]) : 10;
    const int numZones = argc >= 4 ? std::stoi(argv[3]) : 0;
    const int64_t zonePenalty = argc >= 5 ? std::stoll(argv[4]) : 100000;

    try
    {
        const router::Instance inst = router::loadInstance(argv[1]);
        const std::vector<router::Visit> chunks = router::splitCustomers(inst);
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
        if (numZones > 0)
        {
            zoneOf = kMeansZones(inst, numZones);

            std::map<int, std::vector<int64_t>> indicesByZone;
            for (int i = 0; i < numChunks; ++i)
            {
                const int customer = chunks[static_cast<std::size_t>(i)].nodeIndex;
                const int zone = zoneOf[static_cast<std::size_t>(customer)];
                const int64_t index = manager.NodeToIndex(RoutingIndexManager::NodeIndex(i + 1));
                indicesByZone[zone].push_back(index);
            }
            for (auto &[zone, indices] : indicesByZone)
            {
                if (!indices.empty())
                {
                    routing.AddSoftSameVehicleConstraint(indices, zonePenalty);
                }
            }
        }

        RoutingSearchParameters searchParameters = DefaultRoutingSearchParameters();
        searchParameters.set_first_solution_strategy(FirstSolutionStrategy::PATH_CHEAPEST_ARC);
        searchParameters.set_local_search_metaheuristic(LocalSearchMetaheuristic::GUIDED_LOCAL_SEARCH);
        searchParameters.mutable_time_limit()->set_seconds(timeLimitSeconds);

        const auto t0 = std::chrono::steady_clock::now();
        const Assignment *solution = routing.SolveWithParameters(searchParameters);
        const auto t1 = std::chrono::steady_clock::now();
        const double solveMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "n              = " << inst.n << "\n";
        std::cout << "chunks         = " << numChunks << "\n";
        std::cout << "num_zones      = " << numZones << "\n";

        if (solution == nullptr)
        {
            std::cout << "OR-Tools       = NO SOLUTION FOUND (status "
                      << static_cast<int>(routing.status()) << ")\n";
            std::cout << "solve time     = " << solveMs << " ms\n";
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

        std::ifstream f(argv[1]);
        nlohmann::json j;
        f >> j;
        const double referenceCost = j.at("meta").at("difficulty").at("reference_cost").get<double>();

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
        std::cout << "reference_cost = " << referenceCost << "\n";
        std::cout << "vs reference   = " << ((cost - referenceCost) / referenceCost * 100.0) << " %\n";
        std::cout << "or-tools status= " << static_cast<int>(routing.status()) << "\n";
        std::cout << "solve time     = " << solveMs << " ms\n";

        if (numZones > 0)
        {
            std::map<int, std::set<int>> vehiclesPerZone;
            std::vector<int> zonesTouchedPerRoute;
            for (std::size_t r = 0; r < sol.routes.size(); ++r)
            {
                std::set<int> zonesHere;
                for (const router::Visit &stop : sol.routes[r].stops)
                {
                    const int zone = zoneOf[static_cast<std::size_t>(stop.nodeIndex)];
                    zonesHere.insert(zone);
                    vehiclesPerZone[zone].insert(static_cast<int>(r));
                }
                zonesTouchedPerRoute.push_back(static_cast<int>(zonesHere.size()));
            }
            int fragmentation = 0;
            for (const auto &[zone, vehicles] : vehiclesPerZone)
            {
                fragmentation += std::max(0, static_cast<int>(vehicles.size()) - 1);
            }
            const double avgZonesPerRoute =
                zonesTouchedPerRoute.empty()
                    ? 0.0
                    : std::accumulate(zonesTouchedPerRoute.begin(), zonesTouchedPerRoute.end(), 0.0) /
                          static_cast<double>(zonesTouchedPerRoute.size());
            std::cout << "zone fragment. = " << fragmentation << " (sum over zones of extra vehicles used)\n";
            std::cout << "avg zones/route= " << avgZonesPerRoute << "\n";
        }

        return report.feasible ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}