#include <cmath>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "router/cost.hpp"
#include "router/instance_io.hpp"
#include "router/solution_io.hpp"

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: compute_cost <instance.json> <solution.json>\n";
        return 1;
    }

    try
    {
        router::Instance inst = router::loadInstance(argv[1]);
        router::Solution sol = router::loadSolution(argv[2], inst);

        const double cost = router::computeCost(inst, sol);

        std::ifstream f(argv[1]);
        nlohmann::json j;
        f >> j;
        const double referenceCost = j.at("meta").at("difficulty").at("reference_cost").get<double>();

        const double diff = std::abs(cost - referenceCost);

        std::cout << "computeCost()  = " << cost << "\n";
        std::cout << "reference_cost = " << referenceCost << "\n";
        std::cout << "difference     = " << diff << "\n";

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}