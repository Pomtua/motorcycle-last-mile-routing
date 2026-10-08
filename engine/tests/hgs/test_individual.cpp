#include <random>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/route_summary.hpp"
#include "router/split.hpp"

using hgs_test::expect;
using hgs_test::expectInvalidArgument;
using hgs_test::near;

namespace
{
    void testMakeIndividual()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(100);
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog, {hgs_test::makeRandomZones(100, inst, 4), 30.0, 20});
        std::mt19937 rng(100);
        auto routes = hgs_test::makeConflictFreeRoutes(rng, catalog, 0.2);

        std::vector<int> expectedTour;
        router::hgs::CostBreakdown expectedCost;
        for (const auto &route : routes)
        {
            expectedTour.insert(expectedTour.end(), route.begin(), route.end());
            expectedCost += router::hgs::routeCost(data, router::hgs::summarizeRoute(data, route));
        }

        auto withEmpty = routes;
        withEmpty.insert(withEmpty.begin() + 1, std::vector<int>{});
        withEmpty.push_back({});
        const router::hgs::Individual individual = router::hgs::makeIndividual(data, withEmpty);

        expect(individual.routes == routes, "empty routes must be dropped and order kept");
        expect(individual.giantTour == expectedTour, "the giant tour must concatenate routes in order");
        expect(near(individual.cost.distance, expectedCost.distance) &&
                   near(individual.cost.timeWarp, expectedCost.timeWarp) &&
                   individual.cost.zoneExcess == expectedCost.zoneExcess &&
                   individual.cost.routes == static_cast<int>(routes.size()),
               "individual cost must sum route costs");

        bool linksCorrect = individual.successor[0] == 0 && individual.predecessor[0] == 0;
        for (const auto &route : routes)
        {
            for (std::size_t position = 0; position < route.size(); ++position)
            {
                const auto visit = static_cast<std::size_t>(route[position]);
                const int expectedPrevious = position == 0 ? 0 : route[position - 1];
                const int expectedNext = position + 1 == route.size() ? 0 : route[position + 1];
                linksCorrect = linksCorrect &&
                               individual.predecessor[visit] == expectedPrevious &&
                               individual.successor[visit] == expectedNext;
            }
        }
        expect(linksCorrect, "successor and predecessor must link each route through the depot");

        const router::hgs::Individual roundTrip =
            router::hgs::individualFromSolution(data, router::hgs::toSolution(data, individual));
        expect(roundTrip.routes == individual.routes && roundTrip.giantTour == individual.giantTour,
               "exporting and importing a solution must preserve routes");
    }

    void testRejections()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(101, {.customers = 8});
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog);
        std::mt19937 rng(101);
        const auto routes = hgs_test::makeConflictFreeRoutes(rng, catalog, 0.3);

        auto missing = routes;
        missing.back().pop_back();
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, missing); }, "missing visits must be rejected");

        auto duplicated = routes;
        duplicated.front().push_back(duplicated.back().back());
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, duplicated); }, "duplicate visits must be rejected");

        auto outOfRange = routes;
        outOfRange.front().front() = static_cast<int>(data.visitCount()) + 1;
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, outOfRange); }, "unknown visit ids must be rejected");

        int splitVisit = 0;
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()) && splitVisit == 0; ++visit)
        {
            splitVisit = data.siblings(visit).empty() ? 0 : visit;
        }
        expect(splitVisit != 0, "the rejection instance must contain a split customer");
        std::vector<std::vector<int>> conflicting{{}};
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            conflicting.front().push_back(visit);
        }
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, conflicting); },
                              "routes serving two chunks of one customer must be rejected");

        router::Instance smallFleet = inst;
        smallFleet.fleet.size = 1;
        const router::hgs::ProblemData oneVehicle(smallFleet, catalog);
        expectInvalidArgument([&]() { router::hgs::makeIndividual(oneVehicle, routes); }, "routes beyond the fleet must be rejected");

        router::Solution foreign = router::hgs::toSolution(data, router::hgs::makeIndividual(data, routes));
        foreign.routes.front().stops.front().chunkIdx = 99;
        expectInvalidArgument([&]() { router::hgs::individualFromSolution(data, foreign); },
                              "chunks outside the catalog must be rejected");
    }

    void testPermutationCheck()
    {
        expect(router::hgs::isCompletePermutation({3, 1, 2}, 3), "a shuffled range must be a permutation");
        expect(router::hgs::isCompletePermutation({}, 0), "an empty tour must be a permutation of no visits");
        expect(!router::hgs::isCompletePermutation({1, 2}, 3), "short tours must be rejected");
        expect(!router::hgs::isCompletePermutation({1, 1, 2}, 3), "repeated visits must be rejected");
        expect(!router::hgs::isCompletePermutation({0, 1, 2}, 3), "the depot must not appear in a giant tour");
        expect(!router::hgs::isCompletePermutation({1, 2, 4}, 3), "visits beyond the catalog must be rejected");
    }
}

int main()
{
    testMakeIndividual();
    testRejections();
    testPermutationCheck();
    return hgs_test::finish("hgs individual");
}
