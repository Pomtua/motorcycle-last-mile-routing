#include <chrono>
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

    try
    {
        const router::Instance inst = router::loadInstance(argv[1]);

        const auto t0 = std::chrono::steady_clock::now();
        const router::Solution seed = router::solomonI1(inst);
        const auto t1 = std::chrono::steady_clock::now();
        const router::Solution sol = router::localSearch(inst, seed);
        const auto t2 = std::chrono::steady_clock::now();

        const double constructMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double searchMs = std::chrono::duration<double, std::milli>(t2 - t1).count();

        const router::ValidationReport report = router::validate(inst, sol);
        const double seedCost = router::computeCost(inst, seed);
        const double cost = router::computeCost(inst, sol);

        std::ifstream f(argv[1]);
        nlohmann::json j;
        f >> j;
        const double referenceCost =
            j.at("meta").at("difficulty").at("reference_cost").get<double>();

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
        std::cout << "improvement    = "
                  << ((seedCost - cost) / seedCost * 100.0) << " %\n";
        std::cout << "reference_cost = " << referenceCost << "\n";
        std::cout << "vs reference   = "
                  << ((cost - referenceCost) / referenceCost * 100.0) << " %\n";
        std::cout << "construct time = " << constructMs << " ms\n";
        std::cout << "search time    = " << searchMs << " ms\n";
        std::cout << "total time     = " << (constructMs + searchMs) << " ms\n";

        return report.feasible ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
