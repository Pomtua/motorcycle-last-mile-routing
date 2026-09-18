#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "router/cost.hpp"
#include "router/instance_io.hpp"
#include "router/nearest_neighbour.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: run_nn <instance.json>\n";
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
            {"solver", "nn"},
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
            {"runtime_scope", "nearest_neighbour"},
            {"time_budget_ms", nullptr},
            {"status", nullptr},
            {"error", nullptr}
        };

        solverStart = std::chrono::steady_clock::now();
        solverStarted = true;
        const auto start = solverStart;
        const router::Solution sol = router::nearestNeighbour(inst);
        const auto end = std::chrono::steady_clock::now();

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(end - start).count();

        runResult["solved"] = true;
        runResult["runtime_ms"] = elapsedMs;

        const router::ValidationReport report = router::validate(inst, sol);
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
        std::cout << "cost (our NN)  = " << cost << "\n";

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
        std::cout << "time           = " << elapsedMs << " ms\n";
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