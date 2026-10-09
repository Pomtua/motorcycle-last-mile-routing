#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "hgs/search_test_support.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/moves.hpp"
#include "router/hgs/search_routes.hpp"

using hgs_test::expect;
using router::hgs::Penalties;
using router::hgs::ProblemData;
using router::hgs::SearchNode;
using router::hgs::SearchRoutes;

namespace
{
    hgs_test::Fixture smallFixture(std::uint32_t seed, std::mt19937 &rng, hgs_test::RouteLists &lists)
    {
        hgs_test::Fixture fixture = hgs_test::makeFixture(seed, 12, seed % 2 == 0 ? 3 : 0);
        lists = hgs_test::makeConflictFreeRoutes(rng, fixture.catalog, 0.3);
        fixture.inst.fleet.size = static_cast<int>(lists.size()) + 2;
        return fixture;
    }

    bool within(double actual, double expected, double scale)
    {
        return std::abs(actual - expected) <= 1e-9 * std::max(1.0, scale);
    }

    void testSwapAgainstFullEvaluation()
    {
        bool deltasMatch = true;
        bool conflictsMatch = true;
        std::size_t checked = 0;
        std::size_t conflicting = 0;
        for (std::uint32_t seed = 400; seed < 410; ++seed)
        {
            std::mt19937 rng(seed);
            hgs_test::RouteLists lists;
            const hgs_test::Fixture fixture = smallFixture(seed, rng, lists);
            const ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, fixture.zoneOf.empty() ? 0.0 : 80.0, 20});
            const Penalties penalties{12.0, 2500.0, 0.8};
            SearchRoutes routes(data);
            routes.load(router::hgs::makeIndividual(data, lists), penalties);
            const auto current = hgs_test::snapshot(routes);
            const double before = hgs_test::totalCost(data, current, penalties);

            for (int first = 1; first <= static_cast<int>(data.visitCount()); ++first)
            {
                for (int second = 1; second <= static_cast<int>(data.visitCount()); ++second)
                {
                    const SearchNode &u = routes.node(first);
                    const SearchNode &v = routes.node(second);
                    const auto delta = router::hgs::Swap::evaluate(u, v, routes);
                    if (first == second)
                    {
                        deltasMatch = deltasMatch && !delta;
                        continue;
                    }
                    const auto after = hgs_test::swapped(current, first, second);
                    deltasMatch = deltasMatch && delta &&
                                  within(*delta, hgs_test::totalCost(data, after, penalties) - before, before);
                    const bool expectedConflict = hgs_test::hasCustomerConflict(data, after);
                    conflictsMatch = conflictsMatch && router::hgs::Swap::conflicts(u, v, routes) == expectedConflict;
                    conflicting += expectedConflict;
                    ++checked;
                }
            }
        }
        expect(checked > 1000 && conflicting > 0, "the swap check must cover many pairs including conflicts");
        expect(deltasMatch, "swap deltas must equal full re-evaluation for every pair");
        expect(conflictsMatch, "swap conflict detection must match the resulting routes");
    }

    void testTwoOptStarAgainstFullEvaluation()
    {
        bool deltasMatch = true;
        bool conflictsMatch = true;
        std::size_t checked = 0;
        std::size_t conflicting = 0;
        for (std::uint32_t seed = 420; seed < 430; ++seed)
        {
            std::mt19937 rng(seed);
            hgs_test::RouteLists lists;
            const hgs_test::Fixture fixture = smallFixture(seed, rng, lists);
            const ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, fixture.zoneOf.empty() ? 0.0 : 80.0, 20});
            const Penalties penalties{12.0, 2500.0, 0.8};
            SearchRoutes routes(data);
            routes.load(router::hgs::makeIndividual(data, lists), penalties);
            const auto current = hgs_test::snapshot(routes);
            const double before = hgs_test::totalCost(data, current, penalties);

            for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
            {
                const SearchNode &u = routes.node(visit);
                for (const SearchNode *cut : hgs_test::allTargets(routes))
                {
                    const auto delta = router::hgs::TwoOptStar::evaluate(u, *cut, routes);
                    if (cut->route == u.route)
                    {
                        deltasMatch = deltasMatch && !delta;
                        continue;
                    }
                    const auto after = hgs_test::tailsExchanged(current, u, *cut);
                    deltasMatch = deltasMatch && delta &&
                                  within(*delta, hgs_test::totalCost(data, after, penalties) - before, before);
                    const bool expectedConflict = hgs_test::hasCustomerConflict(data, after);
                    conflictsMatch = conflictsMatch && router::hgs::TwoOptStar::conflicts(u, *cut, routes) == expectedConflict;
                    conflicting += expectedConflict;
                    ++checked;
                }
            }
        }
        expect(checked > 1000 && conflicting > 0, "the 2-opt* check must cover many cuts including conflicts");
        expect(deltasMatch, "2-opt* deltas must equal full re-evaluation for every visit and cut");
        expect(conflictsMatch, "2-opt* conflict detection must match the resulting routes");
    }

    template <typename MoveType>
    void checkApplyKeepsInvariants(std::uint32_t seed, const char *name)
    {
        const hgs_test::Fixture fixture = hgs_test::makeFixture(seed, 40, 5);
        const ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, 40.0, 20});
        const Penalties penalties{20.0, 4000.0, 1.0};
        std::mt19937 rng(seed);
        SearchRoutes routes(data);
        routes.load(router::hgs::makeIndividual(data, hgs_test::makeConflictFreeRoutes(rng, fixture.catalog, 0.15)), penalties);
        MoveType move;

        std::uniform_int_distribution<int> pickVisit(1, static_cast<int>(data.visitCount()));
        bool invariantsHold = true;
        bool costsDecrease = true;
        for (int trial = 0; trial < 3000; ++trial)
        {
            SearchNode &u = routes.node(pickVisit(rng));
            SearchNode &v = trial % 10 == 0 && routes.firstEmptyRoute() != nullptr
                                ? routes.firstEmptyRoute()->start
                                : routes.node(pickVisit(rng));
            const double before = routes.totalCost();
            if (!move.tryApply(u, v, routes))
            {
                continue;
            }
            costsDecrease = costsDecrease && routes.totalCost() < before - router::hgs::kImprovementEpsilon / 2.0;
            invariantsHold = invariantsHold && hgs_test::invariantsHold(routes);
        }
        const auto &stats = move.stats();
        expect(stats.applied > 0 && stats.applied + stats.rejectedByConflict == stats.improving,
               std::string(name) + " stats must account for every improving move");
        expect(invariantsHold, std::string(name) + " must keep links, summaries and sibling rules valid");
        expect(costsDecrease, std::string(name) + " must strictly reduce the penalized cost when applied");
    }

    void testMoveNames()
    {
        const auto moves = router::hgs::makeMoves({"relocate", "swap", "2-opt*"});
        expect(moves.size() == 3 && moves[0]->name() == "relocate" && moves[1]->name() == "swap" &&
                   moves[2]->name() == "2-opt*",
               "moves must be built in the configured order with their names");
    }
}

int main()
{
    testSwapAgainstFullEvaluation();
    testTwoOptStarAgainstFullEvaluation();
    checkApplyKeepsInvariants<router::hgs::Swap>(440, "swap");
    checkApplyKeepsInvariants<router::hgs::TwoOptStar>(441, "2-opt*");
    testMoveNames();
    return hgs_test::finish("hgs moves");
}
