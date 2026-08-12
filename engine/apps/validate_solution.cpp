#include <iostream>

#include "router/instance_io.hpp"
#include "router/solution_io.hpp"
#include "router/validate.hpp"

// Usage: validate_solution <instance.json> <solution.json>
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: validate_solution <instance.json> <solution.json>\n";
        return 1;
    }

    try
    {
        router::Instance inst = router::loadInstance(argv[1]);
        router::Solution sol = router::loadSolution(argv[2], inst);

        router::ValidationReport report = router::validate(inst, sol);

        if (report.feasible)
        {
            std::cout << "FEASIBLE — " << sol.routes.size() << " routes, all checks passed.\n";
            return 0;
        }

        std::cout << "INFEASIBLE — " << report.violations.size() << " violation(s):\n";
        for (const auto &v : report.violations)
        {
            std::cout << "  - " << v << "\n";
        }
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}