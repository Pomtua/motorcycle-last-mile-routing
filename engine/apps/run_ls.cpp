#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "router/cost.hpp"
#include "router/instance_io.hpp"
#include "router/local_search.hpp"
#include "router/solomon_i1.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: run_ls <instance.json>\n";
        return 1;
    }

    nlohmann::json runResult;
    bool solverStarted = false;
    std::chrono::steady_clock::time_point solverStart;

    try
    {
        const router::Instance inst = router::loadInstance(argv[1]);

        std::ifstream instanceFile(argv[1]);
        nlohmann::json instanceJson;
        instanceFile >> instanceJson;
        const auto &meta = instanceJson.at("meta");

        runResult = {
            {"schema_version", 1},
            {"instance", argv[1]},
            {"spatial_class", meta.value("spatial_class", "")},
            {"demand_class", meta.value("demand_class", "")},
            {"size", inst.n},
            {"seed", inst.seed},
            {"solver", "i1_ls"},
            {"mode", "SPLIT"},
            {"num_zones", 0},
            {"zone_penalty", nullptr},
            {"solved", false},
            {"valid", nullptr},
            {"distance_cost", nullptr},
            {"route_count", nullptr},
            {"reference_cost", nullptr},
            {"reference_gap_pct", nullptr},
            {"zone_fragmentation", nullptr},
            {"avg_zones_per_route", nullptr},
            {"runtime_ms", nullptr},
            {"runtime_scope", "construction_plus_local_search"},
            {"time_budget_ms", nullptr},
            {"status", nullptr},
            {"error", nullptr}
        };

        solverStart = std::chrono::steady_clock::now();
        solverStarted = true;
        const auto t0 = solverStart;
        const router::Solution seed = router::solomonI1(inst, false);
        const auto t1 = std::chrono::steady_clock::now();
        const router::Solution sol = router::localSearch(inst, seed);
        const auto t2 = std::chrono::steady_clock::now();

        const double constructMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double searchMs = std::chrono::duration<double, std::milli>(t2 - t1).count();

        runResult["solved"] = true;
        runResult["runtime_ms"] = constructMs + searchMs;

        const router::ValidationReport report = router::validate(inst, sol);
        const double seedCost = router::computeCost(inst, seed);
        const double cost = router::computeCost(inst, sol);

        runResult["valid"] = report.feasible;
        runResult["distance_cost"] = cost;
        runResult["route_count"] = sol.routes.size();

        std::cout << "n              = " << inst.n << "\n";
        std::cout << "routes used    = " << sol.routes.size()
                  << " / " << inst.fleet.size << "\n";

        const std::size_t chunks = router::splitCustomers(inst).size();
        std::cout << "chunks         = " << chunks << "\n";
        std::cout << "chunks/route   = "
                  << (sol.routes.empty()
                          ? 0.0
                          : static_cast<double>(chunks) /
                                static_cast<double>(sol.routes.size()))
                  << "\n";

        std::cout << "valid          = " << (report.feasible ? "YES" : "NO") << "\n";
        if (!report.feasible)
        {
            for (const auto &v : report.violations)
            {
                std::cout << "    - " << v << "\n";
            }
        }
        std::cout << "cost (I1 seed) = " << seedCost << "\n";
        std::cout << "cost (I1 + LS) = " << cost << "\n";
        if (std::isfinite(seedCost) && seedCost > 0.0)
        {
            std::cout << "improvement    = "
                      << ((seedCost - cost) / seedCost * 100.0) << " %\n";
        }

        if (meta.contains("difficulty") &&
            meta["difficulty"].contains("reference_cost") &&
            meta["difficulty"]["reference_cost"].is_number())
        {
            const double referenceCost =
                meta["difficulty"]["reference_cost"].get<double>();
            if (std::isfinite(referenceCost) && referenceCost > 0.0)
            {
                const double gapPct =
                    (cost - referenceCost) / referenceCost * 100.0;
                runResult["reference_cost"] = referenceCost;
                runResult["reference_gap_pct"] = gapPct;
                std::cout << "reference_cost = " << referenceCost << "\n";
                std::cout << "vs reference   = " << gapPct << " %\n";
            }
        }
        std::cout << "construct time = " << constructMs << " ms\n";
        std::cout << "search time    = " << searchMs << " ms\n";
        std::cout << "total time     = " << (constructMs + searchMs) << " ms\n";
        std::cout << "RESULT_JSON " << runResult.dump() << "\n";

        return report.feasible ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        if (solverStarted)
        {
            if (runResult["runtime_ms"].is_null())
            {
                runResult["runtime_ms"] =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - solverStart).count();
            }
            runResult["error"] = e.what();
            std::cout << "RESULT_JSON " << runResult.dump() << "\n";
        }
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
