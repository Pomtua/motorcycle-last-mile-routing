#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "router/hgs.hpp"
#include "router/hgs/problem_data.hpp"
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
    if (argc != 2 && argc != 3)
    {
        std::cerr << "usage: bench_split_v2 <instance.json> [tours]\n";
        return 2;
    }

    try
    {
        const int tours = argc == 3 ? std::stoi(argv[2]) : 50;
        if (tours <= 0 || tours > 100000)
        {
            throw std::invalid_argument("tours must be between 1 and 100000");
        }

        const router::Instance inst = router::loadInstance(argv[1]);
        const std::vector<router::Visit> catalog = router::splitCustomers(inst);
        const router::HgsPenaltyWeights v1Penalties = router::makeInitialHgsPenalties(inst, catalog);
        const router::hgs::Penalties penalties{
            v1Penalties.weightPenalty, v1Penalties.volumePenalty, v1Penalties.timeWarpPenalty};

        const auto setupStart = Clock::now();
        const router::hgs::ProblemData data(inst, catalog);
        const double setupMs = elapsedMs(setupStart, Clock::now());

        std::mt19937 rng(42);
        std::vector<int> tour(data.visitCount());
        for (std::size_t index = 0; index < tour.size(); ++index)
        {
            tour[index] = static_cast<int>(index) + 1;
        }

        double v1Ms = 0.0;
        double v2Ms = 0.0;
        double maxRelativeGap = 0.0;
        int failureMismatches = 0;
        for (int trial = 0; trial < tours; ++trial)
        {
            std::shuffle(tour.begin(), tour.end(), rng);
            std::vector<router::HgsGene> genes;
            genes.reserve(tour.size());
            for (int visit : tour)
            {
                genes.push_back({static_cast<std::size_t>(visit) - 1});
            }

            std::optional<double> v1Cost;
            const auto v1Start = Clock::now();
            try
            {
                v1Cost = router::decodeGiantTour(inst, catalog, genes, v1Penalties).evaluation.penalizedCost;
            }
            catch (const router::HgsSplitFailure &)
            {
            }
            const auto v2Start = Clock::now();
            const auto decoded = router::hgs::split(data, tour, penalties);
            const auto v2End = Clock::now();
            v1Ms += elapsedMs(v1Start, v2Start);
            v2Ms += elapsedMs(v2Start, v2End);

            if (v1Cost.has_value() != decoded.has_value())
            {
                ++failureMismatches;
                continue;
            }
            if (decoded)
            {
                const double v2Cost = router::hgs::penalizedCost(data, decoded->cost, penalties);
                maxRelativeGap = std::max(maxRelativeGap, std::abs(v2Cost - *v1Cost) / std::max(1.0, std::abs(*v1Cost)));
            }
        }

        std::cout << "instance          = " << argv[1] << "\n";
        std::cout << "visits            = " << data.visitCount() << "\n";
        std::cout << "fleet             = " << data.fleetSize() << "\n";
        std::cout << "tours             = " << tours << "\n";
        std::cout << "v2 setup ms       = " << setupMs << "\n";
        std::cout << "v1 split avg ms   = " << v1Ms / tours << "\n";
        std::cout << "v2 split avg ms   = " << v2Ms / tours << "\n";
        std::cout << "speedup           = " << (v2Ms > 0.0 ? v1Ms / v2Ms : 0.0) << "x\n";
        std::cout << "max relative gap  = " << maxRelativeGap << "\n";
        std::cout << "failure mismatch  = " << failureMismatches << "\n";
        return failureMismatches == 0 && maxRelativeGap <= 1e-9 ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
