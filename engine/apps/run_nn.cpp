#include <chrono>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "router/cost.hpp"
#include "router/instance_io.hpp"
#include "router/nearest_neighbour.hpp"
#include "router/validate.hpp"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: run_nn <instance.json>\n";
        return 1;
    }

    try
    {
        const router::Instance inst = router::loadInstance(argv[1]);

        const auto start = std::chrono::steady_clock::now();
        const router::Solution sol = router::nearestNeighbour(inst);
        const auto end = std::chrono::steady_clock::now();

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(end - start).count();

        const router::ValidationReport report = router::validate(inst, sol);
        const double cost = router::computeCost(inst, sol);

        std::ifstream f(argv[1]);
        nlohmann::json j;
        f >> j;
        const double referenceCost =
            j.at("meta").at("difficulty").at("reference_cost").get<double>();

        std::cout << "n              = " << inst.n << "\n";
        std::cout << "routes used    = " << sol.routes.size()
                  << " / " << inst.fleet.size << "\n";
        std::cout << "valid          = " << (report.feasible ? "YES" : "NO") << "\n";
        if (!report.feasible)
        {
            for (const auto &v : report.violations)
            {
                std::cout << "    - " << v << "\n";
            }
        }
        std::cout << "cost (our NN)  = " << cost << "\n";
        std::cout << "reference_cost = " << referenceCost << "\n";
        std::cout << "vs reference   = "
                  << ((cost - referenceCost) / referenceCost * 100.0) << " %\n";
        std::cout << "time           = " << elapsedMs << " ms\n";

        return report.feasible ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}