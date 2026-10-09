#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/local_search.hpp"
#include "router/hgs/moves.hpp"
#include "router/hgs/penalty_controller.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/search_context.hpp"
#include "router/hgs/split.hpp"
#include "router/instance_io.hpp"
#include "router/split.hpp"

namespace
{
    using Clock = std::chrono::steady_clock;

    double elapsedMs(Clock::time_point start, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4)
    {
        std::cerr << "usage: bench_local_search <instance.json> [tours] [--moves=a,b,c]\n";
        return 2;
    }

    try
    {
        int tours = 20;
        std::vector<std::string> moveNames{"relocate", "swap", "2-opt*"};
        for (int index = 2; index < argc; ++index)
        {
            const std::string arg = argv[index];
            if (arg.starts_with("--moves="))
            {
                moveNames.clear();
                std::stringstream list(arg.substr(8));
                for (std::string name; std::getline(list, name, ',');)
                {
                    moveNames.push_back(name);
                }
            }
            else
            {
                tours = std::stoi(arg);
            }
        }
        if (tours <= 0 || tours > 10000)
        {
            throw std::invalid_argument("tours must be between 1 and 10000");
        }

        const router::Instance inst = router::loadInstance(argv[1]);
        const router::hgs::ProblemData data(inst, router::splitCustomers(inst));
        const router::hgs::Penalties penalties = router::hgs::initialPenalties(data);
        router::hgs::LocalSearch search(data, router::hgs::makeMoves(moveNames));
        router::hgs::Deadline noDeadline;

        std::mt19937 rng(42);
        std::vector<int> tour(data.visitCount());
        std::iota(tour.begin(), tour.end(), 1);

        int decoded = 0;
        int feasible = 0;
        double splitCostSum = 0.0;
        double searchCostSum = 0.0;
        double searchMs = 0.0;
        for (int trial = 0; trial < tours; ++trial)
        {
            std::shuffle(tour.begin(), tour.end(), rng);
            auto individual = router::hgs::split(data, tour, penalties);
            if (!individual)
            {
                continue;
            }
            ++decoded;
            splitCostSum += router::hgs::penalizedCost(data, individual->cost, penalties);

            const auto start = Clock::now();
            search.run(*individual, penalties, rng, noDeadline);
            searchMs += elapsedMs(start, Clock::now());
            searchCostSum += router::hgs::penalizedCost(data, individual->cost, penalties);
            feasible += router::hgs::isFeasible(individual->cost);
        }
        if (decoded == 0)
        {
            throw std::runtime_error("no random tour could be decoded");
        }

        const auto &stats = search.stats();
        std::cout << "instance            = " << argv[1] << "\n";
        std::cout << "visits              = " << data.visitCount() << "\n";
        std::cout << "decoded tours       = " << decoded << " / " << tours << "\n";
        std::cout << "split avg cost      = " << splitCostSum / decoded << "\n";
        std::cout << "search avg ms       = " << searchMs / decoded << "\n";
        std::cout << "search avg cost     = " << searchCostSum / decoded << "\n";
        std::cout << "feasible            = " << feasible << " / " << decoded << "\n";
        std::cout << "avg passes          = " << static_cast<double>(stats.passes) / decoded << "\n";
        std::cout << "pairs skipped       = " << stats.pairsSkipped << "\n";
        for (const auto &move : search.moves())
        {
            const auto &moveStats = move->stats();
            std::cout << "move " << move->name() << ": evaluated=" << moveStats.evaluated
                      << " improving=" << moveStats.improving
                      << " conflicts=" << moveStats.rejectedByConflict
                      << " applied=" << moveStats.applied << "\n";
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
