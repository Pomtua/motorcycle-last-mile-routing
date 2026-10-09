#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/penalty_controller.hpp"
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
        std::cerr << "usage: bench_split <instance.json> [tours]\n";
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
        const auto setupStart = Clock::now();
        const router::hgs::ProblemData data(inst, router::splitCustomers(inst));
        const double setupMs = elapsedMs(setupStart, Clock::now());
        const router::hgs::Penalties penalties = router::hgs::initialPenalties(data);

        std::mt19937 rng(42);
        std::vector<int> tour(data.visitCount());
        std::iota(tour.begin(), tour.end(), 1);

        double splitMs = 0.0;
        double costSum = 0.0;
        int decoded = 0;
        for (int trial = 0; trial < tours; ++trial)
        {
            std::shuffle(tour.begin(), tour.end(), rng);
            const auto start = Clock::now();
            const auto individual = router::hgs::split(data, tour, penalties);
            splitMs += elapsedMs(start, Clock::now());
            if (individual)
            {
                ++decoded;
                costSum += router::hgs::penalizedCost(data, individual->cost, penalties);
            }
        }

        std::cout << "instance          = " << argv[1] << "\n";
        std::cout << "visits            = " << data.visitCount() << "\n";
        std::cout << "fleet             = " << data.fleetSize() << "\n";
        std::cout << "tours             = " << tours << "\n";
        std::cout << "setup ms          = " << setupMs << "\n";
        std::cout << "split avg ms      = " << splitMs / tours << "\n";
        std::cout << "decoded tours     = " << decoded << " / " << tours << "\n";
        if (decoded > 0)
        {
            std::cout << "avg penalized cost= " << costSum / decoded << "\n";
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
