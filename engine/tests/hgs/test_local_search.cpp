#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/local_search.hpp"
#include "router/hgs/moves.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/route_summary.hpp"
#include "router/hgs/search_context.hpp"
#include "router/hgs/search_routes.hpp"
#include "router/hgs/split.hpp"
#include "router/split.hpp"

using hgs_test::expect;
using router::hgs::Individual;
using router::hgs::Penalties;
using router::hgs::ProblemData;
using router::hgs::SearchNode;
using router::hgs::SearchRoutes;

namespace
{
    struct Fixture
    {
        router::Instance inst;
        std::vector<router::Visit> catalog;
        std::vector<int> zoneOf;
    };

    Fixture makeFixture(std::uint32_t seed, int customers, int zones)
    {
        Fixture fixture;
        fixture.inst = hgs_test::makeRandomInstance(seed, {.customers = customers, .minWindow = 900.0, .maxWindow = 7200.0});
        fixture.catalog = router::splitCustomers(fixture.inst);
        if (zones > 0)
        {
            fixture.zoneOf = hgs_test::makeRandomZones(seed, fixture.inst, zones);
        }
        return fixture;
    }

    std::vector<std::vector<int>> snapshot(const SearchRoutes &routes)
    {
        std::vector<std::vector<int>> lists(static_cast<std::size_t>(routes.routeCount()));
        for (int index = 0; index < routes.routeCount(); ++index)
        {
            const auto &route = routes.route(index);
            for (const SearchNode *node = route.start.next; node != &route.end; node = node->next)
            {
                lists[static_cast<std::size_t>(index)].push_back(node->visit);
            }
        }
        return lists;
    }

    double totalCost(const ProblemData &data, const std::vector<std::vector<int>> &lists, const Penalties &penalties)
    {
        double total = 0.0;
        for (const auto &list : lists)
        {
            total += router::hgs::penalizedCost(data, router::hgs::summarizeRoute(data, list), penalties);
        }
        return total;
    }

    std::vector<std::vector<int>> relocated(std::vector<std::vector<int>> lists, int visit, const SearchNode &target)
    {
        for (auto &list : lists)
        {
            list.erase(std::remove(list.begin(), list.end(), visit), list.end());
        }
        auto &destination = lists[static_cast<std::size_t>(target.route)];
        const auto at = target.isDepot()
                            ? destination.begin()
                            : std::find(destination.begin(), destination.end(), target.visit) + 1;
        destination.insert(at, visit);
        return lists;
    }

    std::vector<const SearchNode *> allTargets(const SearchRoutes &routes)
    {
        std::vector<const SearchNode *> targets;
        for (int visit = 1; visit <= static_cast<int>(routes.data().visitCount()); ++visit)
        {
            targets.push_back(&routes.node(visit));
        }
        for (int index = 0; index < routes.routeCount(); ++index)
        {
            targets.push_back(&routes.route(index).start);
        }
        return targets;
    }

    Individual randomIndividual(const ProblemData &data, const Fixture &fixture, std::mt19937 &rng, double newRouteProbability)
    {
        return router::hgs::makeIndividual(data, hgs_test::makeConflictFreeRoutes(rng, fixture.catalog, newRouteProbability));
    }

    void testLoadAndExport()
    {
        const Fixture fixture = makeFixture(300, 30, 4);
        const ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, 60.0, 20});
        const Penalties penalties{15.0, 3000.0, 2.0};
        std::mt19937 rng(300);
        const Individual individual = randomIndividual(data, fixture, rng, 0.2);

        SearchRoutes routes(data);
        routes.load(individual, penalties);
        bool consistent = true;
        try
        {
            routes.checkInvariants();
        }
        catch (const std::logic_error &)
        {
            consistent = false;
        }
        expect(consistent, "loaded routes must satisfy every cached-summary invariant");
        expect(routes.exportRoutes() == individual.routes, "export must return the loaded routes in order");
        expect(hgs_test::near(routes.totalCost(), router::hgs::penalizedCost(data, individual.cost, penalties)),
               "cached route costs must add up to the individual's penalized cost");

        Individual broken = individual;
        broken.routes.front().push_back(broken.routes.back().front());
        hgs_test::expectInvalidArgument([&]() { routes.load(broken, penalties); }, "loading duplicated visits must be rejected");
    }

    void testRelocateDeltaAgainstFullEvaluation()
    {
        bool deltasMatch = true;
        bool noOpsOnlyWhenExpected = true;
        std::size_t checked = 0;
        for (std::uint32_t seed = 310; seed < 320; ++seed)
        {
            Fixture fixture = makeFixture(seed, 12, seed % 2 == 0 ? 3 : 0);
            std::mt19937 rng(seed);
            const auto lists = hgs_test::makeConflictFreeRoutes(rng, fixture.catalog, 0.3);
            fixture.inst.fleet.size = static_cast<int>(lists.size()) + 2;
            const ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, fixture.zoneOf.empty() ? 0.0 : 80.0, 20});
            const Penalties penalties{10.0 + seed % 5, 2500.0, 0.7};

            SearchRoutes routes(data);
            routes.load(router::hgs::makeIndividual(data, lists), penalties);
            const auto current = snapshot(routes);
            const double before = totalCost(data, current, penalties);

            for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
            {
                const SearchNode &u = routes.node(visit);
                for (const SearchNode *target : allTargets(routes))
                {
                    const auto delta = router::hgs::Relocate::evaluate(u, *target, routes);
                    if (!delta)
                    {
                        noOpsOnlyWhenExpected = noOpsOnlyWhenExpected && (target == &u || target->next == &u);
                        continue;
                    }
                    const double expected = totalCost(data, relocated(current, visit, *target), penalties) - before;
                    deltasMatch = deltasMatch && std::abs(*delta - expected) <= 1e-9 * std::max(1.0, before);
                    ++checked;
                }
            }
        }
        expect(checked > 1000, "the relocate delta check must cover many moves");
        expect(deltasMatch, "relocate deltas must equal full re-evaluation for every visit and position");
        expect(noOpsOnlyWhenExpected, "relocate must only skip moves that leave the route unchanged");
    }

    void testRelocateApplyKeepsInvariants()
    {
        const Fixture fixture = makeFixture(330, 40, 5);
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
            try
            {
                routes.checkInvariants();
            }
            catch (const std::logic_error &)
            {
                invariantsHold = false;
            }
        }
        const auto &stats = relocate.stats();
        expect(stats.applied > 0 && stats.applied + stats.rejectedByConflict == stats.improving,
               "relocate stats must account for every improving move");
        expect(stats.rejectedByConflict > 0, "the apply check must exercise same-customer conflicts");
        expect(invariantsHold, "applied relocates must keep links, summaries and sibling rules valid");
        expect(costsDecrease, "every applied relocate must strictly reduce the penalized cost");
    }

    void testLocalSearchReachesLocalOptimum()
    {
        bool optimal = true;
        bool neverWorse = true;
        bool deterministic = true;
        for (std::uint32_t seed = 340; seed < 346; ++seed)
        {
            const Fixture fixture = makeFixture(seed, 25, seed % 2 == 0 ? 4 : 0);
            const ProblemData data(
                fixture.inst, fixture.catalog,
                {fixture.zoneOf, fixture.zoneOf.empty() ? 0.0 : 50.0, fixture.catalog.size()});
            const Penalties penalties{8.0, 2000.0, 0.5};
            std::mt19937 rng(seed);
            const Individual start = randomIndividual(data, fixture, rng, 0.25);

            router::hgs::LocalSearch search(data, router::hgs::makeMoves({"relocate"}));
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
            for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
            {
                const SearchNode &u = routes.node(visit);
                for (const SearchNode *target : allTargets(routes))
                {
                    const auto delta = router::hgs::Relocate::evaluate(u, *target, routes);
                    const bool blocked = target->route != u.route && routes.hasSiblingInRoute(visit, target->route);
                    optimal = optimal && (!delta || *delta >= -router::hgs::kImprovementEpsilon || blocked);
                }
            }
        }
        expect(optimal, "local search with full neighbourhoods must stop at a relocate local optimum");
        expect(neverWorse, "local search must never return a worse individual");
        expect(deterministic, "local search must be reproducible with the same random seed");
    }

    void testDeadlineAndConfiguration()
    {
        const Fixture fixture = makeFixture(350, 60, 0);
        const ProblemData data(fixture.inst, fixture.catalog);
        const Penalties penalties{5.0, 1000.0, 0.5};
        std::mt19937 rng(350);
        const Individual start = randomIndividual(data, fixture, rng, 0.05);

        router::hgs::LocalSearch search(data, router::hgs::makeMoves({"relocate"}));
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
        const Fixture fixture = makeFixture(360, 80, 0);
        const ProblemData data(fixture.inst, fixture.catalog);
        const Penalties penalties{10.0, 2000.0, 1.0};
        std::mt19937 rng(360);
        std::vector<int> tour(data.visitCount());
        for (std::size_t index = 0; index < tour.size(); ++index)
        {
            tour[index] = static_cast<int>(index) + 1;
        }

        router::hgs::LocalSearch search(data, router::hgs::makeMoves({"relocate"}));
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
    }
}

int main()
{
    testLoadAndExport();
    testRelocateDeltaAgainstFullEvaluation();
    testRelocateApplyKeepsInvariants();
    testLocalSearchReachesLocalOptimum();
    testDeadlineAndConfiguration();
    testSplitThenSearchImproves();
    return hgs_test::finish("hgs local_search");
}
