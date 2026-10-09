#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/route_summary.hpp"
#include "router/hgs/split.hpp"
#include "router/split.hpp"

using hgs_test::expect;
using hgs_test::near;

namespace
{
    std::vector<int> shuffledTour(std::mt19937 &rng, std::size_t visitCount)
    {
        std::vector<int> tour(visitCount);
        for (std::size_t index = 0; index < visitCount; ++index)
        {
            tour[index] = static_cast<int>(index) + 1;
        }
        std::shuffle(tour.begin(), tour.end(), rng);
        return tour;
    }

    std::optional<double> bruteForceSplit(
        const router::hgs::ProblemData &data,
        const std::vector<int> &tour,
        const router::hgs::Penalties &penalties)
    {
        const std::size_t cuts = tour.size() - 1;
        std::optional<double> best;
        for (std::uint32_t mask = 0; mask < (std::uint32_t{1} << cuts); ++mask)
        {
            std::vector<std::vector<int>> routes{{tour.front()}};
            for (std::size_t index = 1; index < tour.size(); ++index)
            {
                if (mask & (std::uint32_t{1} << (index - 1)))
                {
                    routes.push_back({});
                }
                routes.back().push_back(tour[index]);
            }
            if (routes.size() > static_cast<std::size_t>(data.fleetSize()))
            {
                continue;
            }
            bool allowed = true;
            double cost = 0.0;
            for (const auto &route : routes)
            {
                for (std::size_t first = 0; first < route.size() && allowed; ++first)
                {
                    for (std::size_t second = first + 1; second < route.size() && allowed; ++second)
                    {
                        allowed = data.visit(route[first]).node != data.visit(route[second]).node;
                    }
                }
                const auto summary = router::hgs::summarizeRoute(data, route);
                allowed = allowed &&
                          summary.weight <= router::hgs::kSplitLoadLimitFactor * data.weightCapacity() + router::hgs::kWeightTolerance &&
                          summary.volume <= router::hgs::kSplitLoadLimitFactor * data.volumeCapacity() + router::hgs::kVolumeTolerance;
                cost += router::hgs::penalizedCost(data, summary, penalties);
            }
            if (allowed && (!best || cost < *best))
            {
                best = cost;
            }
        }
        return best;
    }

    void testOptimalAgainstBruteForce()
    {
        bool optimal = true;
        int failuresMatched = 0;
        for (std::uint32_t seed = 200; seed < 230; ++seed)
        {
            router::Instance inst = hgs_test::makeRandomInstance(seed, {.customers = 7, .weightCapacity = 20.0, .minWindow = 900.0, .maxWindow = 5400.0});
            inst.fleet.size = 1 + static_cast<int>(seed % 4);
            const auto catalog = router::splitCustomers(inst);
            if (catalog.size() > 14)
            {
                continue;
            }
            const router::hgs::ProblemData data(inst, catalog, {hgs_test::makeRandomZones(seed, inst, 3), 40.0, 20});
            const router::hgs::Penalties penalties{5.0 + seed % 7, 2000.0, 0.5 + 0.1 * (seed % 5)};
            std::mt19937 rng(seed);
            for (int trial = 0; trial < 5; ++trial)
            {
                const auto tour = shuffledTour(rng, data.visitCount());
                const auto expected = bruteForceSplit(data, tour, penalties);
                const auto decoded = router::hgs::split(data, tour, penalties);
                if (!expected || !decoded)
                {
                    optimal = optimal && !expected && !decoded;
                    failuresMatched += !expected && !decoded;
                    continue;
                }
                optimal = optimal &&
                          near(router::hgs::penalizedCost(data, decoded->cost, penalties), *expected, 1e-9) &&
                          decoded->giantTour == tour &&
                          decoded->cost.routes <= data.fleetSize();
            }
        }
        expect(optimal, "Split must find the cheapest fleet-bounded partition of the giant tour");
        expect(failuresMatched > 0, "the brute-force check must include tours with no valid partition");
    }

    void testNeverWorseThanSourceRoutes()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(260, {.customers = 50});
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog);
        const router::hgs::Penalties penalties{10.0, 1000.0, 1.0};
        std::mt19937 rng(260);

        bool neverWorse = true;
        for (int trial = 0; trial < 20; ++trial)
        {
            const auto source = router::hgs::makeIndividual(data, hgs_test::makeConflictFreeRoutes(rng, catalog, 0.15));
            const auto decoded = router::hgs::split(data, source.giantTour, penalties);
            neverWorse = neverWorse && decoded &&
                         router::hgs::penalizedCost(data, decoded->cost, penalties) <=
                             router::hgs::penalizedCost(data, source.cost, penalties) + 1e-9;
        }
        expect(neverWorse, "Split must never be worse than the routes that produced its giant tour");
    }

    void testEdgeCases()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(270, {.customers = 5});
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog);
        const router::hgs::Penalties penalties{1.0, 1.0, 1.0};

        std::mt19937 rng(270);
        const auto tour = shuffledTour(rng, data.visitCount());
        hgs_test::expectInvalidArgument([&]() { router::hgs::split(data, {1, 2}, penalties); },
                                        "Split must reject incomplete giant tours");
        hgs_test::expectInvalidArgument([&]() { router::hgs::split(data, tour, {-1.0, 0.0, 0.0}); },
                                        "Split must reject negative penalties");

        router::Instance noFleet = inst;
        noFleet.fleet.size = 0;
        const router::hgs::ProblemData noVehicles(noFleet, catalog);
        expect(!router::hgs::split(noVehicles, tour, penalties),
               "Split must report no partition without vehicles");

        router::Instance empty;
        empty.horizon = 100.0;
        empty.fleet = {0, 1.0, 1.0};
        empty.nodes.resize(1);
        empty.distanceMatrix = {{0.0}};
        empty.durationMatrix = {{0.0}};
        const router::hgs::ProblemData emptyData(empty, {});
        const auto emptySplit = router::hgs::split(emptyData, {}, penalties);
        expect(emptySplit && emptySplit->routes.empty(), "an empty tour must split into no routes");
    }
}

int main()
{
    testOptimalAgainstBruteForce();
    testNeverWorseThanSourceRoutes();
    testEdgeCases();
    return hgs_test::finish("hgs split");
}
