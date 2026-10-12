#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/route_summary.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"

using hgs_test::expect;
using hgs_test::expectInvalidArgument;
using hgs_test::near;
using router::hgs::Delivery;

namespace
{
    void testMakeIndividual()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(100);
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog, {hgs_test::makeRandomZones(100, inst, 4), 30.0, 20});
        std::mt19937 rng(100);
        const auto routes = hgs_test::toDeliveries(data, hgs_test::makeConflictFreeRoutes(rng, catalog, 0.2));

        std::vector<Delivery> expectedTour;
        std::vector<router::hgs::Arc> expectedArcs;
        router::hgs::CostBreakdown expectedCost;
        for (const auto &route : routes)
        {
            expectedTour.insert(expectedTour.end(), route.begin(), route.end());
            expectedCost += router::hgs::routeCost(data, router::hgs::summarizeRoute(data, route));
            int previous = 0;
            for (const Delivery &delivery : route)
            {
                expectedArcs.emplace_back(previous, delivery.customer);
                previous = delivery.customer;
            }
            expectedArcs.emplace_back(previous, 0);
        }
        std::sort(expectedArcs.begin(), expectedArcs.end());

        auto withEmpty = routes;
        withEmpty.insert(withEmpty.begin() + 1, std::vector<Delivery>{});
        withEmpty.push_back({});
        const router::hgs::Individual individual = router::hgs::makeIndividual(data, withEmpty);

        expect(individual.routes == routes, "empty routes must be dropped and order kept");
        expect(individual.giantTour == expectedTour, "the giant tour must concatenate routes in order");
        expect(individual.arcs == expectedArcs, "arcs must list every customer-to-customer link including the depot");
        expect(near(individual.cost.distance, expectedCost.distance) &&
                   near(individual.cost.timeWarp, expectedCost.timeWarp) &&
                   individual.cost.zoneExcess == expectedCost.zoneExcess &&
                   individual.cost.routes == static_cast<int>(routes.size()),
               "individual cost must sum route costs");

        const router::Solution exported = router::hgs::toSolution(data, individual);
        bool exportMatches = exported.routes.size() == routes.size();
        for (std::size_t r = 0; exportMatches && r < routes.size(); ++r)
        {
            for (std::size_t s = 0; s < routes[r].size(); ++s)
            {
                const router::Visit &stop = exported.routes[r].stops[s];
                exportMatches = exportMatches &&
                                stop.nodeIndex == routes[r][s].customer &&
                                stop.weight == router::hgs::toWeight(routes[r][s].units) &&
                                near(stop.volume, data.volumeOf(routes[r][s]));
            }
        }
        expect(exportMatches, "exported stops must carry each delivery's customer, weight and proportional volume");
        expect(router::hgs::individualFromSolution(data, exported).routes == individual.routes,
               "exporting and importing a solution must preserve routes");
    }

    void testQuantitiesNeedNotMatchCanonicalChunks()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(101, {.customers = 6, .horizon = 1e6, .maxDemandRatio = 0.8, .minWindow = 1e6, .maxWindow = 1e6});
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog);

        int customer = 0;
        for (int candidate = 1; candidate <= inst.n && customer == 0; ++candidate)
        {
            customer = data.customer(candidate).demandUnits >= 3 ? candidate : 0;
        }
        std::vector<std::vector<Delivery>> routes;
        const std::int64_t demand = data.customer(customer).demandUnits;
        routes.push_back({{customer, demand / 3}});
        routes.push_back({{customer, demand - demand / 3}});
        for (int other = 1; other <= inst.n; ++other)
        {
            if (other != customer)
            {
                routes.push_back({{other, data.customer(other).demandUnits}});
            }
        }
        const router::hgs::Individual individual = router::hgs::makeIndividual(data, routes);
        expect(individual.giantTour.size() == static_cast<std::size_t>(inst.n) + 1,
               "a customer may be served in any number of positive parts that add up to its demand");
        expect(router::hgs::isFeasible(individual.cost) &&
                   router::validate(inst, router::hgs::toSolution(data, individual)).feasible,
               "a split chosen by the solver must be feasible and pass the quantity-based validator");
    }

    void testRejections()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(102, {.customers = 8});
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog);
        std::mt19937 rng(102);
        const auto routes = hgs_test::toDeliveries(data, hgs_test::makeConflictFreeRoutes(rng, catalog, 0.3));

        auto missing = routes;
        missing.back().pop_back();
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, missing); }, "uncovered demand must be rejected");

        auto overDelivered = routes;
        overDelivered.front().front().units += 1;
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, overDelivered); }, "over-delivered demand must be rejected");

        auto zero = routes;
        zero.front().push_back({zero.front().front().customer == 1 ? 2 : 1, 0});
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, zero); }, "zero-quantity deliveries must be rejected");

        auto unknown = routes;
        unknown.front().front().customer = inst.n + 1;
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, unknown); }, "unknown customers must be rejected");

        std::vector<std::vector<Delivery>> merged{{}};
        for (const auto &route : routes)
        {
            merged.front().insert(merged.front().end(), route.begin(), route.end());
        }
        expect(merged.front().size() > static_cast<std::size_t>(inst.n), "the rejection instance must contain a split customer");
        expectInvalidArgument([&]() { router::hgs::makeIndividual(data, merged); },
                              "routes visiting one customer twice must be rejected");

        router::Instance smallFleet = inst;
        smallFleet.fleet.size = 1;
        const router::hgs::ProblemData oneVehicle(smallFleet, catalog);
        expectInvalidArgument([&]() { router::hgs::makeIndividual(oneVehicle, routes); }, "routes beyond the fleet must be rejected");

        router::Solution foreign = router::hgs::toSolution(data, router::hgs::makeIndividual(data, routes));
        foreign.routes.front().stops.front().nodeIndex = 0;
        expectInvalidArgument([&]() { router::hgs::individualFromSolution(data, foreign); },
                              "stops at the depot must be rejected");
    }

    void testQuantityValidator()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(104, {.customers = 8});
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog);
        std::mt19937 rng(104);
        const auto tokenRoutes = hgs_test::makeConflictFreeRoutes(rng, catalog, 1.0);

        router::Solution canonical;
        for (const auto &route : tokenRoutes)
        {
            router::Route exported;
            for (int token : route)
            {
                exported.stops.push_back(catalog[static_cast<std::size_t>(token) - 1]);
            }
            canonical.routes.push_back(exported);
        }
        const auto coverageViolations = [&](const router::Solution &solution)
        {
            int count = 0;
            for (const auto &violation : router::validate(inst, solution).violations)
            {
                count += violation.find("receives") != std::string::npos ||
                         violation.find("proportional") != std::string::npos ||
                         violation.find("twice") != std::string::npos ||
                         violation.find("non-positive") != std::string::npos;
            }
            return count;
        };
        expect(coverageViolations(canonical) == 0, "canonical chunks from splitCustomers must satisfy the quantity checks");

        router::Solution under = canonical;
        under.routes.front().stops.front().weight -= 0.01;
        expect(coverageViolations(under) > 0, "under-delivered weight must be reported");

        router::Solution over = canonical;
        over.routes.front().stops.front().weight += 0.01;
        expect(coverageViolations(over) > 0, "over-delivered weight must be reported");

        router::Solution disproportionate = canonical;
        disproportionate.routes.front().stops.front().volume *= 0.5;
        expect(coverageViolations(disproportionate) > 0, "volume out of proportion to weight must be reported");

        router::Solution doubled = canonical;
        doubled.routes.front().stops.push_back(doubled.routes.front().stops.front());
        doubled.routes.front().stops.front().weight = 0.0;
        expect(coverageViolations(doubled) > 0, "visiting a customer twice in one route must be reported");
    }

    void testDeliverySetCheck()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(103, {.customers = 4});
        const router::hgs::ProblemData data(inst, router::splitCustomers(inst));
        std::vector<Delivery> all;
        for (int token = 1; token <= static_cast<int>(data.tokenCount()); ++token)
        {
            all.push_back(data.token(token));
        }
        expect(router::hgs::isCompleteDeliverySet(data, all), "the canonical chunks must form a complete delivery set");
        std::reverse(all.begin(), all.end());
        expect(router::hgs::isCompleteDeliverySet(data, all), "order must not matter for a delivery set");
        all.pop_back();
        expect(!router::hgs::isCompleteDeliverySet(data, all), "missing deliveries must be detected");
    }
}

int main()
{
    testMakeIndividual();
    testQuantitiesNeedNotMatchCanonicalChunks();
    testRejections();
    testQuantityValidator();
    testDeliverySetCheck();
    return hgs_test::finish("hgs individual");
}
