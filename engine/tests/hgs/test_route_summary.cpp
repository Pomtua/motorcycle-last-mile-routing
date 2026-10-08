#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/route_summary.hpp"
#include "hgs/test_support.hpp"
#include "router/hgs.hpp"
#include "router/solution.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"

using hgs_test::expect;
using hgs_test::near;
using router::hgs::RouteSummary;

namespace
{
    bool sameSummary(const RouteSummary &a, const RouteSummary &b)
    {
        return a.first == b.first && a.last == b.last && a.visits == b.visits && a.zones == b.zones &&
               near(a.distance, b.distance, 1e-9) && near(a.duration, b.duration, 1e-9) &&
               near(a.timeWarp, b.timeWarp, 1e-9) && near(a.earliest, b.earliest, 1e-9) &&
               near(a.latest, b.latest, 1e-9) && near(a.weight, b.weight, 1e-9) &&
               near(a.volume, b.volume, 1e-9);
    }

    RouteSummary foldVisits(const router::hgs::ProblemData &data, const std::vector<int> &visits)
    {
        RouteSummary summary = router::hgs::visitSummary(data, visits.front());
        for (std::size_t index = 1; index < visits.size(); ++index)
        {
            summary = router::hgs::concat(data, summary, router::hgs::visitSummary(data, visits[index]));
        }
        return summary;
    }

    std::vector<int> randomVisits(std::mt19937 &rng, const router::hgs::ProblemData &data, std::size_t count)
    {
        std::uniform_int_distribution<int> pick(1, static_cast<int>(data.visitCount()));
        std::vector<int> visits(count);
        for (int &visit : visits)
        {
            visit = pick(rng);
        }
        return visits;
    }

    void testAssociativity()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(10);
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog, {hgs_test::makeRandomZones(10, inst, 6), 50.0, 20});
        std::mt19937 rng(10);
        std::uniform_int_distribution<std::size_t> length(1, 6);

        bool associative = true;
        for (int trial = 0; trial < 2000; ++trial)
        {
            const RouteSummary a = foldVisits(data, randomVisits(rng, data, length(rng)));
            const RouteSummary b = foldVisits(data, randomVisits(rng, data, length(rng)));
            const RouteSummary c = foldVisits(data, randomVisits(rng, data, length(rng)));
            associative = associative &&
                          sameSummary(router::hgs::concat(data, router::hgs::concat(data, a, b), c),
                                      router::hgs::concat(data, a, router::hgs::concat(data, b, c)));
        }
        expect(associative, "concat must be associative so any split of a route gives the same summary");
    }

    void testPrefixSuffixComposition()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(11);
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog);
        std::mt19937 rng(11);
        std::uniform_int_distribution<std::size_t> length(1, 12);

        bool splitsMatch = true;
        bool insertionsMatch = true;
        for (int trial = 0; trial < 500; ++trial)
        {
            const std::vector<int> route = randomVisits(rng, data, length(rng));
            const RouteSummary whole = router::hgs::summarizeRoute(data, route);
            for (std::size_t cut = 0; cut <= route.size(); ++cut)
            {
                RouteSummary prefix = router::hgs::depotSummary(data);
                for (std::size_t index = 0; index < cut; ++index)
                {
                    prefix = router::hgs::concat(data, prefix, router::hgs::visitSummary(data, route[index]));
                }
                RouteSummary suffix = router::hgs::depotSummary(data);
                for (std::size_t index = route.size(); index > cut; --index)
                {
                    suffix = router::hgs::concat(data, router::hgs::visitSummary(data, route[index - 1]), suffix);
                }
                splitsMatch = splitsMatch && sameSummary(router::hgs::concat(data, prefix, suffix), whole);

                const int inserted = randomVisits(rng, data, 1).front();
                std::vector<int> expanded = route;
                expanded.insert(expanded.begin() + static_cast<std::ptrdiff_t>(cut), inserted);
                const RouteSummary composed = router::hgs::concat(
                    data, router::hgs::concat(data, prefix, router::hgs::visitSummary(data, inserted)), suffix);
                insertionsMatch = insertionsMatch && sameSummary(composed, router::hgs::summarizeRoute(data, expanded));
            }
        }
        expect(splitsMatch, "prefix + suffix must reproduce the whole route at every cut");
        expect(insertionsMatch, "prefix + visit + suffix must equal evaluating the expanded route");
    }

    void testAgainstV1SegmentEvaluation()
    {
        bool matches = true;
        for (std::uint32_t seed = 20; seed < 30; ++seed)
        {
            const router::Instance inst = hgs_test::makeRandomInstance(seed, {.customers = 40, .minWindow = 300.0, .maxWindow = 3600.0});
            const auto catalog = router::splitCustomers(inst);
            const std::vector<int> zoneOf = hgs_test::makeRandomZones(seed, inst, 7);
            const router::hgs::ProblemData data(inst, catalog, {zoneOf, 0.0, 20});
            std::mt19937 rng(seed);

            for (const auto &route : hgs_test::makeConflictFreeRoutes(rng, catalog, 0.1))
            {
                std::vector<router::HgsGene> genes;
                for (int visit : route)
                {
                    genes.push_back({static_cast<std::size_t>(visit) - 1});
                }
                const auto v1 = router::evaluateGiantTourSegment(inst, catalog, genes, 0, genes.size(), zoneOf);
                const auto cost = router::hgs::routeCost(data, router::hgs::summarizeRoute(data, route));
                matches = matches &&
                          near(cost.distance, v1.distanceCost, 1e-9) &&
                          near(cost.timeWarp, v1.violations.timeWarp, 1e-9) &&
                          near(cost.weightExcess, v1.violations.weightExcess, 1e-9) &&
                          near(cost.volumeExcess, v1.violations.volumeExcess, 1e-9) &&
                          cost.zoneExcess == v1.routeZoneExcess;
            }
        }
        expect(matches, "route summaries must match v1 segment evaluation on distance, load, time warp and zones");
    }

    void testAgainstValidator()
    {
        int feasibleSeen = 0;
        int infeasibleSeen = 0;
        bool agrees = true;
        for (std::uint32_t seed = 40; seed < 70; ++seed)
        {
            const router::Instance inst = hgs_test::makeRandomInstance(seed, {.customers = 25, .minWindow = 3600.0, .maxWindow = 14400.0});
            const auto catalog = router::splitCustomers(inst);
            const router::hgs::ProblemData data(inst, catalog);
            std::mt19937 rng(seed);

            for (double newRouteProbability : {0.15, 0.4, 1.0})
            {
                const auto routes = hgs_test::makeConflictFreeRoutes(rng, catalog, newRouteProbability);
                router::Solution solution;
                router::hgs::CostBreakdown total;
                for (const auto &route : routes)
                {
                    router::Route exported;
                    for (int visit : route)
                    {
                        exported.stops.push_back(data.catalogVisit(visit));
                    }
                    solution.routes.push_back(exported);
                    total += router::hgs::routeCost(data, router::hgs::summarizeRoute(data, route));
                }
                const bool validatorFeasible = router::validate(inst, solution).feasible;
                agrees = agrees && validatorFeasible == router::hgs::isFeasible(total) &&
                         total.routes == static_cast<int>(routes.size());
                (validatorFeasible ? feasibleSeen : infeasibleSeen) += 1;
            }
        }
        expect(agrees, "cost-model feasibility must agree with the independent validator");
        expect(feasibleSeen > 0 && infeasibleSeen > 0, "the validator oracle must see both feasible and infeasible solutions");
    }

    void testCostModel()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(80, {.customers = 6});
        const auto catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog, {hgs_test::makeRandomZones(80, inst, 3), 25.0, 20});

        expect(router::hgs::routeCost(data, router::hgs::summarizeRoute(data, std::vector<int>{})).routes == 0,
               "an empty route must cost nothing and not count as a route");

        router::hgs::CostBreakdown cost;
        cost.distance = 100.0;
        cost.zoneExcess = 2;
        cost.weightExcess = 3.0;
        cost.volumeExcess = 0.5;
        cost.timeWarp = 7.0;
        const router::hgs::Penalties penalties{10.0, 100.0, 2.0};
        expect(router::hgs::objectiveCost(data, cost) == 150.0, "objective must add the zone penalty per extra zone");
        expect(router::hgs::penalizedCost(data, cost, penalties) == 150.0 + 30.0 + 50.0 + 14.0,
               "penalized cost must add each weighted violation");
        expect(!router::hgs::isFeasible(cost), "violations must make a cost infeasible");

        router::hgs::CostBreakdown withinTolerance;
        withinTolerance.timeWarp = router::hgs::kTimeTolerance;
        expect(router::hgs::isFeasible(withinTolerance), "violations within tolerance must stay feasible");

        expect(router::hgs::excessAbove(30.0 + 1e-7, 30.0, router::hgs::kWeightTolerance) == 0.0 &&
                   router::hgs::excessAbove(31.0, 30.0, router::hgs::kWeightTolerance) == 1.0,
               "excess must ignore tolerance noise and report the full overflow otherwise");

        hgs_test::expectInvalidArgument([]() { router::hgs::validatePenalties({-1.0, 0.0, 0.0}); },
                                        "negative penalties must be rejected");
        hgs_test::expectInvalidArgument([]() { router::hgs::validatePenalties({0.0, std::nan(""), 0.0}); },
                                        "NaN penalties must be rejected");
    }
}

int main()
{
    testAssociativity();
    testPrefixSuffixComposition();
    testAgainstV1SegmentEvaluation();
    testAgainstValidator();
    testCostModel();
    return hgs_test::finish("hgs route_summary");
}
