#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "hgs/search_test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/local_search.hpp"
#include "router/hgs/moves.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/search_context.hpp"
#include "router/hgs/search_routes.hpp"
#include "router/hgs/split.hpp"

using hgs_test::expect;
using router::hgs::Individual;
using router::hgs::Penalties;
using router::hgs::ProblemData;
using router::hgs::SearchNode;
using router::hgs::SearchRoutes;

namespace
{
    const std::vector<std::string> kAllMoves{"relocate", "swap", "2-opt*"};

    Individual randomIndividual(const ProblemData &data, const hgs_test::Fixture &fixture, std::mt19937 &rng, double newRouteProbability)
    {
        return router::hgs::makeIndividual(data, hgs_test::makeConflictFreeRoutes(rng, fixture.catalog, newRouteProbability));
    }

    bool isSibling(const ProblemData &data, int visit, const SearchNode &other)
    {
        return !other.isDepot() && data.visit(other.visit).node == data.visit(visit).node;
    }

    void testLoadAndExport()
    {
        const hgs_test::Fixture fixture = hgs_test::makeFixture(300, 30, 4);
        const ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, 60.0, 20});
        const Penalties penalties{15.0, 3000.0, 2.0};
        std::mt19937 rng(300);
        const Individual individual = randomIndividual(data, fixture, rng, 0.2);

        SearchRoutes routes(data);
        routes.load(individual, penalties);
        expect(hgs_test::invariantsHold(routes), "loaded routes must satisfy every cached-summary invariant");
        expect(routes.exportRoutes() == individual.routes, "export must return the loaded routes in order");
        expect(hgs_test::near(routes.totalCost(), router::hgs::penalizedCost(data, individual.cost, penalties)),
               "cached route costs must add up to the individual's penalized cost");

        Individual broken = individual;
        broken.routes.front().push_back(broken.routes.back().front());
        hgs_test::expectInvalidArgument([&]() { routes.load(broken, penalties); }, "loading duplicated visits must be rejected");
    }

    void testRelocateAgainstFullEvaluation()
    {
        bool deltasMatch = true;
        bool conflictsMatch = true;
        bool noOpsOnlyWhenExpected = true;
        std::size_t checked = 0;
        std::size_t conflicting = 0;
        for (std::uint32_t seed = 310; seed < 320; ++seed)
        {
            hgs_test::Fixture fixture = hgs_test::makeFixture(seed, 12, seed % 2 == 0 ? 3 : 0);
            std::mt19937 rng(seed);
            const auto lists = hgs_test::makeConflictFreeRoutes(rng, fixture.catalog, 0.3);
            fixture.inst.fleet.size = static_cast<int>(lists.size()) + 2;
            const ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, fixture.zoneOf.empty() ? 0.0 : 80.0, 20});
            const Penalties penalties{10.0 + seed % 5, 2500.0, 0.7};

            SearchRoutes routes(data);
            routes.load(router::hgs::makeIndividual(data, lists), penalties);
            const auto current = hgs_test::snapshot(routes);
            const double before = hgs_test::totalCost(data, current, penalties);

            for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
            {
                const SearchNode &u = routes.node(visit);
                for (const SearchNode *target : hgs_test::allTargets(routes))
                {
                    const auto delta = router::hgs::Relocate::evaluate(u, *target, routes);
                    if (!delta)
                    {
                        noOpsOnlyWhenExpected = noOpsOnlyWhenExpected && (target == &u || target->next == &u);
                        continue;
                    }
                    const auto after = hgs_test::relocated(current, visit, *target);
                    const double expected = hgs_test::totalCost(data, after, penalties) - before;
                    deltasMatch = deltasMatch && std::abs(*delta - expected) <= 1e-9 * std::max(1.0, before);
                    const bool expectedConflict = hgs_test::hasCustomerConflict(data, after);
                    conflictsMatch = conflictsMatch && router::hgs::Relocate::conflicts(u, *target, routes) == expectedConflict;
                    conflicting += expectedConflict;
                    ++checked;
                }
            }
        }
        expect(checked > 1000 && conflicting > 0, "the relocate check must cover many moves including conflicts");
        expect(deltasMatch, "relocate deltas must equal full re-evaluation for every visit and position");
        expect(conflictsMatch, "relocate conflict detection must match the resulting routes");
        expect(noOpsOnlyWhenExpected, "relocate must only skip moves that leave the route unchanged");
    }

    void testRelocateApplyKeepsInvariants()
    {
        const hgs_test::Fixture fixture = hgs_test::makeFixture(330, 40, 5);
        const ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, 40.0, 20});
        const Penalties penalties{20.0, 4000.0, 1.0};
        std::mt19937 rng(330);
        SearchRoutes routes(data);
        routes.load(randomIndividual(data, fixture, rng, 0.15), penalties);
        router::hgs::Relocate relocate;

        std::uniform_int_distribution<int> pickVisit(1, static_cast<int>(data.visitCount()));
        bool invariantsHold = true;
        bool costsDecrease = true;
        for (int trial = 0; trial < 3000; ++trial)
        {
            SearchNode &u = routes.node(pickVisit(rng));
            SearchNode &v = routes.node(pickVisit(rng));
            const double before = routes.totalCost();
            if (!relocate.tryApply(u, v, routes))
            {
                continue;
            }
            costsDecrease = costsDecrease && routes.totalCost() < before - router::hgs::kImprovementEpsilon / 2.0;
            invariantsHold = invariantsHold && hgs_test::invariantsHold(routes);
        }
        const auto &stats = relocate.stats();
        expect(stats.applied > 0 && stats.applied + stats.rejectedByConflict == stats.improving,
               "relocate stats must account for every improving move");
        expect(stats.rejectedByConflict > 0, "the apply check must exercise same-customer conflicts");
        expect(invariantsHold, "applied relocates must keep links, summaries and sibling rules valid");
        expect(costsDecrease, "every applied relocate must strictly reduce the penalized cost");
    }

    bool noImprovingMoveLeft(const ProblemData &data, const SearchRoutes &routes)
    {
        const auto improving = [](const std::optional<double> &delta)
        { return delta && *delta < -router::hgs::kImprovementEpsilon; };

        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            const SearchNode &u = routes.node(visit);
            for (const SearchNode *target : hgs_test::allTargets(routes))
            {
                if (improving(router::hgs::Relocate::evaluate(u, *target, routes)) &&
                    !router::hgs::Relocate::conflicts(u, *target, routes))
                {
                    return false;
                }

                const bool cutBeforeSibling = target->next != nullptr && isSibling(data, visit, *target->next) && target->isDepot();
                if (!isSibling(data, visit, *target) && !cutBeforeSibling &&
                    improving(router::hgs::TwoOptStar::evaluate(u, *target, routes)) &&
                    !router::hgs::TwoOptStar::conflicts(u, *target, routes))
                {
                    return false;
                }

                if (!target->isDepot() && !isSibling(data, visit, *target) &&
                    improving(router::hgs::Swap::evaluate(u, *target, routes)) &&
                    !router::hgs::Swap::conflicts(u, *target, routes))
                {
                    return false;
                }
            }
        }
        return true;
    }

    void testLocalSearchReachesLocalOptimum()
    {
        bool optimal = true;
        bool neverWorse = true;
        bool deterministic = true;
        for (std::uint32_t seed = 340; seed < 346; ++seed)
        {
            const hgs_test::Fixture fixture = hgs_test::makeFixture(seed, 25, seed % 2 == 0 ? 4 : 0);
            const ProblemData data(
                fixture.inst, fixture.catalog,
                {fixture.zoneOf, fixture.zoneOf.empty() ? 0.0 : 50.0, fixture.catalog.size()});
            const Penalties penalties{8.0, 2000.0, 0.5};
            std::mt19937 rng(seed);
            const Individual start = randomIndividual(data, fixture, rng, 0.25);

            router::hgs::LocalSearch search(data, router::hgs::makeMoves(kAllMoves));
            search.setInvariantChecks(true);
            router::hgs::Deadline noDeadline;
            Individual improved = start;
            router::hgs::Rng searchRng(seed);
            search.run(improved, penalties, searchRng, noDeadline);

            Individual repeated = start;
            router::hgs::Rng repeatRng(seed);
            search.run(repeated, penalties, repeatRng, noDeadline);
            deterministic = deterministic && repeated.routes == improved.routes;

            neverWorse = neverWorse &&
                         router::hgs::penalizedCost(data, improved.cost, penalties) <=
                             router::hgs::penalizedCost(data, start.cost, penalties) + 1e-9;

            SearchRoutes routes(data);
            routes.load(improved, penalties);
            optimal = optimal && noImprovingMoveLeft(data, routes);
        }
        expect(optimal, "local search with full neighbourhoods must stop where no relocate, swap or 2-opt* improves");
        expect(neverWorse, "local search must never return a worse individual");
        expect(deterministic, "local search must be reproducible with the same random seed");
    }

    void testDeadlineAndConfiguration()
    {
        const hgs_test::Fixture fixture = hgs_test::makeFixture(350, 60, 0);
        const ProblemData data(fixture.inst, fixture.catalog);
        const Penalties penalties{5.0, 1000.0, 0.5};
        std::mt19937 rng(350);
        const Individual start = randomIndividual(data, fixture, rng, 0.05);

        router::hgs::LocalSearch search(data, router::hgs::makeMoves(kAllMoves));
        router::hgs::Deadline expired(router::hgs::SteadyClock::now() - std::chrono::seconds(1));
        Individual interrupted = start;
        router::hgs::Rng searchRng(350);
        search.run(interrupted, penalties, searchRng, expired);
        expect(search.stats().interruptedByDeadline == 1 && search.stats().runs == 1,
               "an expired deadline must interrupt the search at the first amortized check");
        expect(router::hgs::penalizedCost(data, interrupted.cost, penalties) <=
                   router::hgs::penalizedCost(data, start.cost, penalties) + 1e-9,
               "an interrupted search must still return a complete, no-worse individual");

        hgs_test::expectInvalidArgument([]() { router::hgs::makeMoves({}); }, "an empty move list must be rejected");
        hgs_test::expectInvalidArgument([]() { router::hgs::makeMoves({"two-opt"}); }, "unknown move names must be rejected");
    }

    void testSplitThenSearchImproves()
    {
        const hgs_test::Fixture fixture = hgs_test::makeFixture(360, 80, 0);
        const ProblemData data(fixture.inst, fixture.catalog);
        const Penalties penalties{10.0, 2000.0, 1.0};
        std::mt19937 rng(360);
        std::vector<int> tour(data.visitCount());
        for (std::size_t index = 0; index < tour.size(); ++index)
        {
            tour[index] = static_cast<int>(index) + 1;
        }

        router::hgs::LocalSearch search(data, router::hgs::makeMoves(kAllMoves));
        router::hgs::Deadline noDeadline;
        bool improves = true;
        for (int trial = 0; trial < 10; ++trial)
        {
            std::shuffle(tour.begin(), tour.end(), rng);
            auto decoded = router::hgs::split(data, tour, penalties);
            if (!decoded)
            {
                continue;
            }
            const double before = router::hgs::penalizedCost(data, decoded->cost, penalties);
            search.run(*decoded, penalties, rng, noDeadline);
            improves = improves && router::hgs::penalizedCost(data, decoded->cost, penalties) < before;
        }
        expect(improves, "local search must improve Split decodings of random tours");
        expect(search.stats().pairsSkipped > 0, "later passes must skip pairs whose routes did not change");
        bool everyMoveUsed = true;
        for (const auto &move : search.moves())
        {
            everyMoveUsed = everyMoveUsed && move->stats().applied > 0;
        }
        expect(everyMoveUsed, "relocate, swap and 2-opt* must each contribute improvements");
    }
}

int main()
{
    testLoadAndExport();
    testRelocateAgainstFullEvaluation();
    testRelocateApplyKeepsInvariants();
    testLocalSearchReachesLocalOptimum();
    testDeadlineAndConfiguration();
    testSplitThenSearchImproves();
    return hgs_test::finish("hgs local_search");
}
