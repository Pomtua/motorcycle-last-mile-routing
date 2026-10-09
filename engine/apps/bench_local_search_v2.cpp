#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "router/hgs.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/local_search.hpp"
#include "router/hgs/moves.hpp"
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
        std::cerr << "usage: bench_local_search_v2 <instance.json> [tours] [--skip-v1]\n";
        return 2;
    }

    try
    {
        int tours = 20;
        bool runV1 = true;
        for (int index = 2; index < argc; ++index)
        {
            const std::string arg = argv[index];
            if (arg == "--skip-v1")
            {
                runV1 = false;
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
        const std::vector<router::Visit> catalog = router::splitCustomers(inst);
        const router::HgsPenaltyWeights v1Penalties = router::makeInitialHgsPenalties(inst, catalog);
        const router::hgs::Penalties penalties{
            v1Penalties.weightPenalty, v1Penalties.volumePenalty, v1Penalties.timeWarpPenalty};
        const router::hgs::ProblemData data(inst, catalog);
        router::hgs::LocalSearch search(data, router::hgs::makeMoves({"relocate"}));
        router::hgs::Deadline noDeadline;

        std::mt19937 rng(42);
        std::vector<int> tour(data.visitCount());
        std::iota(tour.begin(), tour.end(), 1);

        int decoded = 0;
        int v2Feasible = 0;
        int v1Feasible = 0;
        double splitCostSum = 0.0;
        double v2CostSum = 0.0;
        double v1CostSum = 0.0;
        double v2Ms = 0.0;
        double v1Ms = 0.0;
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
            const router::Solution start = router::hgs::toSolution(data, *individual);

            const auto v2Start = Clock::now();
            search.run(*individual, penalties, rng, noDeadline);
            v2Ms += elapsedMs(v2Start, Clock::now());
            v2CostSum += router::hgs::penalizedCost(data, individual->cost, penalties);
            v2Feasible += router::hgs::isFeasible(individual->cost);

            if (runV1)
            {
                const auto v1Start = Clock::now();
                const router::HgsIndividual educated = router::educateHgsIndividual(
                    inst, catalog, router::makeHgsIndividualFromSolution(inst, catalog, start, v1Penalties),
                    v1Penalties, 100);
                v1Ms += elapsedMs(v1Start, Clock::now());
                v1CostSum += educated.evaluation.penalizedCost;
                v1Feasible += educated.evaluation.feasible;
            }
        }
        if (decoded == 0)
        {
            throw std::runtime_error("no random tour could be decoded");
        }

        const auto &stats = search.stats();
        const auto &relocate = search.moves().front()->stats();
        std::cout << "instance            = " << argv[1] << "\n";
        std::cout << "visits              = " << data.visitCount() << "\n";
        std::cout << "decoded tours       = " << decoded << " / " << tours << "\n";
        std::cout << "split avg cost      = " << splitCostSum / decoded << "\n";
        std::cout << "v2 avg ms           = " << v2Ms / decoded << "\n";
        std::cout << "v2 avg cost         = " << v2CostSum / decoded << "\n";
        std::cout << "v2 feasible         = " << v2Feasible << " / " << decoded << "\n";
        std::cout << "v2 avg passes       = " << static_cast<double>(stats.passes) / decoded << "\n";
        std::cout << "v2 pairs skipped    = " << stats.pairsSkipped << "\n";
        std::cout << "relocate evaluated  = " << relocate.evaluated << "\n";
        std::cout << "relocate improving  = " << relocate.improving << "\n";
        std::cout << "relocate conflicts  = " << relocate.rejectedByConflict << "\n";
        std::cout << "relocate applied    = " << relocate.applied << "\n";
        if (runV1)
        {
            std::cout << "v1 avg ms (cap 100) = " << v1Ms / decoded << "\n";
            std::cout << "v1 avg cost         = " << v1CostSum / decoded << "\n";
            std::cout << "v1 feasible         = " << v1Feasible << " / " << decoded << "\n";
            std::cout << "speedup             = " << (v2Ms > 0.0 ? v1Ms / v2Ms : 0.0) << "x\n";
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
