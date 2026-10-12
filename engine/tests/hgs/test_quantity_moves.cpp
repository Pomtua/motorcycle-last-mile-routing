#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "hgs/search_test_support.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/local_search.hpp"
#include "router/hgs/moves.hpp"
#include "router/hgs/search_context.hpp"
#include "router/hgs/search_routes.hpp"
#include "router/validate.hpp"

using hgs_test::expect;
using router::hgs::Delivery;
using router::hgs::Penalties;
using router::hgs::ProblemData;
using router::hgs::SearchNode;
using router::hgs::SearchRoutes;

namespace
{
    const std::vector<std::string> kFixedMoves{"relocate", "swap", "2-opt*"};
    const std::vector<std::string> kDynamicMoves{"relocate", "swap", "2-opt*", "partial-relocate", "transfer", "split-insertion"};

    bool within(double actual, double expected, double scale)
    {
        return std::abs(actual - expected) <= 1e-9 * std::max(1.0, scale);
    }

    std::vector<std::int64_t> sampleAmounts(std::int64_t units)
    {
        std::vector<std::int64_t> amounts;
        for (std::int64_t amount : {std::int64_t{1}, units / 2, units - 1})
        {
            if (amount >= 1 && amount < units && std::find(amounts.begin(), amounts.end(), amount) == amounts.end())
            {
                amounts.push_back(amount);
            }
        }
        return amounts;
    }

    struct Loaded
    {
        hgs_test::Fixture fixture;
        ProblemData data;
        Penalties penalties;
    };

    Loaded makeLoaded(std::uint32_t seed, int customers, double newRouteProbability, std::vector<std::vector<int>> &tokenRoutes)
    {
        hgs_test::Fixture fixture = hgs_test::makeFixture(seed, customers, seed % 2 == 0 ? 3 : 0);
        std::mt19937 rng(seed);
        tokenRoutes = hgs_test::makeConflictFreeRoutes(rng, fixture.catalog, newRouteProbability);
        fixture.inst.fleet.size = static_cast<int>(tokenRoutes.size()) + 2;
        ProblemData data(fixture.inst, fixture.catalog, {fixture.zoneOf, fixture.zoneOf.empty() ? 0.0 : 80.0, 20});
        return {fixture, data, {12.0, 2500.0, 0.8}};
    }

    void testPoolOperations()
    {
        std::vector<std::vector<int>> tokenRoutes;
        const Loaded loaded = makeLoaded(900, 20, 0.3, tokenRoutes);
        SearchRoutes routes(loaded.data);
        routes.load(hgs_test::individualFromTokens(loaded.data, tokenRoutes), loaded.penalties);
        std::mt19937 rng(900);

        bool invariantsHold = true;
        int splits = 0;
        int merges = 0;
        for (int trial = 0; trial < 500; ++trial)
        {
            const std::vector<int> ids = hgs_test::sortedActiveNodes(routes);
            SearchNode &u = routes.node(ids[std::uniform_int_distribution<std::size_t>(0, ids.size() - 1)(rng)]);
            const auto &siblings = routes.nodesOf(u.customer);
            if (siblings.size() > 1 && trial % 2 == 0)
            {
                SearchNode &other = routes.node(siblings.front() == u.id ? siblings.back() : siblings.front());
                const int fromRoute = u.route;
                routes.setUnits(other, other.units + u.units);
                routes.remove(u);
                routes.update(routes.route(fromRoute));
                routes.update(routes.route(other.route));
                ++merges;
            }
            else if (u.units >= 2 && routes.freeNodeCount() > 0)
            {
                for (int index = 0; index < routes.routeCount(); ++index)
                {
                    if (index != u.route && !routes.hasCustomerInRoute(u.customer, index))
                    {
                        const std::int64_t moved = u.units / 2;
                        const int fromRoute = u.route;
                        routes.setUnits(u, u.units - moved);
                        routes.insertAfter({u.customer, moved}, routes.route(index).start);
                        routes.update(routes.route(index));
                        routes.update(routes.route(fromRoute));
                        ++splits;
                        break;
                    }
                }
            }
            invariantsHold = invariantsHold && hgs_test::invariantsHold(routes);
        }
        expect(splits > 20 && merges > 20, "the pool check must exercise many splits and merges");
        expect(invariantsHold, "inserting, removing and re-quantifying nodes must keep every invariant");

        bool exhausted = false;
        try
        {
            for (std::size_t attempt = 0; attempt <= routes.nodeCapacity(); ++attempt)
            {
                const std::vector<int> ids = hgs_test::sortedActiveNodes(routes);
                SearchNode &u = routes.node(ids.front());
                routes.insertAfter({u.customer, 1}, routes.route(0).start);
            }
        }
        catch (const std::length_error &)
        {
            exhausted = true;
        }
        expect(exhausted, "allocating past the pool capacity must throw");
    }

    void testPartialRelocateAgainstFullEvaluation()
    {
        bool deltasMatch = true;
        bool skipsOnlyWhenExpected = true;
        std::size_t checked = 0;
        for (std::uint32_t seed = 910; seed < 916; ++seed)
        {
            std::vector<std::vector<int>> tokenRoutes;
            const Loaded loaded = makeLoaded(seed, 10, 0.3, tokenRoutes);
            SearchRoutes routes(loaded.data);
            routes.load(hgs_test::individualFromTokens(loaded.data, tokenRoutes), loaded.penalties);
            const auto current = hgs_test::snapshot(routes);
            const double before = hgs_test::totalCost(loaded.data, current, loaded.penalties);

            for (int id : hgs_test::sortedActiveNodes(routes))
            {
                const SearchNode &u = routes.node(id);
                for (const SearchNode *target : hgs_test::allTargets(routes))
                {
                    const bool allowed = target->route != u.route && !routes.hasCustomerInRoute(u.customer, target->route);
                    for (std::int64_t moved : sampleAmounts(u.units))
                    {
                        const auto delta = router::hgs::PartialRelocate::evaluate(u, *target, moved, routes);
                        if (!allowed)
                        {
                            skipsOnlyWhenExpected = skipsOnlyWhenExpected && !delta;
                            continue;
                        }
                        auto after = current;
                        after[static_cast<std::size_t>(u.route)][hgs_test::indexOf(u)].units -= moved;
                        auto &destination = after[static_cast<std::size_t>(target->route)];
                        destination.insert(destination.begin() + target->position, Delivery{u.customer, moved});
                        deltasMatch = deltasMatch && delta &&
                                      within(*delta, hgs_test::totalCost(loaded.data, after, loaded.penalties) - before, before);
                        ++checked;
                    }
                }
            }
        }
        expect(checked > 1000, "the partial relocate check must cover many moves");
        expect(deltasMatch, "partial relocate deltas must equal full re-evaluation");
        expect(skipsOnlyWhenExpected, "partial relocate must skip only same-route and sibling-route targets");
    }

    void testTransferAgainstFullEvaluation()
    {
        bool deltasMatch = true;
        std::size_t checked = 0;
        for (std::uint32_t seed = 920; seed < 930; ++seed)
        {
            std::vector<std::vector<int>> tokenRoutes;
            const Loaded loaded = makeLoaded(seed, 12, 0.3, tokenRoutes);
            SearchRoutes routes(loaded.data);
            routes.load(hgs_test::individualFromTokens(loaded.data, tokenRoutes), loaded.penalties);
            const auto current = hgs_test::snapshot(routes);
            const double before = hgs_test::totalCost(loaded.data, current, loaded.penalties);

            for (int id : hgs_test::sortedActiveNodes(routes))
            {
                const SearchNode &u = routes.node(id);
                for (int sibling : routes.nodesOf(u.customer))
                {
                    if (sibling == id)
                    {
                        expect(!router::hgs::Transfer::evaluate(u, u, 1, routes), "transfer to itself must be skipped");
                        continue;
                    }
                    const SearchNode &s = routes.node(sibling);
                    for (std::int64_t moved : sampleAmounts(u.units))
                    {
                        const auto delta = router::hgs::Transfer::evaluate(u, s, moved, routes);
                        auto after = current;
                        after[static_cast<std::size_t>(u.route)][hgs_test::indexOf(u)].units -= moved;
                        after[static_cast<std::size_t>(s.route)][hgs_test::indexOf(s)].units += moved;
                        deltasMatch = deltasMatch && delta &&
                                      within(*delta, hgs_test::totalCost(loaded.data, after, loaded.penalties) - before, before);
                        ++checked;
                    }
                }
            }
        }
        expect(checked > 50, "the transfer check needs customers served in several parts");
        expect(deltasMatch, "transfer deltas must equal full re-evaluation");
    }

    void testSplitInsertionPlans()
    {
        bool deltasMatch = true;
        bool piecesValid = true;
        std::size_t planned = 0;
        std::size_t merged = 0;
        std::size_t multiPiece = 0;
        for (std::uint32_t seed = 930; seed < 940; ++seed)
        {
            std::vector<std::vector<int>> tokenRoutes;
            const Loaded loaded = makeLoaded(seed, 14, 0.5, tokenRoutes);
            SearchRoutes routes(loaded.data);
            routes.load(hgs_test::individualFromTokens(loaded.data, tokenRoutes), loaded.penalties);
            const auto current = hgs_test::snapshot(routes);
            const double before = hgs_test::totalCost(loaded.data, current, loaded.penalties);

            for (int id : hgs_test::sortedActiveNodes(routes))
            {
                SearchNode &u = routes.node(id);
                const auto plan = router::hgs::SplitInsertion::plan(u, routes);
                if (!plan)
                {
                    continue;
                }
                ++planned;
                multiPiece += plan->pieces.size() > 1;

                auto after = current;
                after[static_cast<std::size_t>(u.route)].erase(after[static_cast<std::size_t>(u.route)].begin() +
                                                               static_cast<std::ptrdiff_t>(hgs_test::indexOf(u)));
                std::int64_t total = 0;
                std::vector<int> pieceRoutes;
                for (const auto &piece : plan->pieces)
                {
                    total += piece.units;
                    pieceRoutes.push_back(piece.target->route);
                    piecesValid = piecesValid && piece.units > 0 && piece.target->route != u.route;
                    auto &destination = after[static_cast<std::size_t>(piece.target->route)];
                    if (piece.merge)
                    {
                        ++merged;
                        piecesValid = piecesValid && piece.target->customer == u.customer;
                        destination[hgs_test::indexOf(*piece.target)].units += piece.units;
                    }
                    else
                    {
                        destination.insert(destination.begin() + piece.target->position, Delivery{u.customer, piece.units});
                    }
                }
                std::sort(pieceRoutes.begin(), pieceRoutes.end());
                piecesValid = piecesValid && total == u.units &&
                              plan->pieces.size() <= router::hgs::kMaxSplitRoutes &&
                              std::adjacent_find(pieceRoutes.begin(), pieceRoutes.end()) == pieceRoutes.end() &&
                              !hgs_test::hasCustomerConflict(after);
                deltasMatch = deltasMatch &&
                              within(plan->delta, hgs_test::totalCost(loaded.data, after, loaded.penalties) - before, before);
            }
        }
        expect(planned > 50 && merged > 0 && multiPiece > 0, "the split insertion check must include merges and multi-route splits");
        expect(piecesValid, "split insertion pieces must cover the whole quantity in distinct conflict-free routes");
        expect(deltasMatch, "split insertion deltas must equal full re-evaluation");
    }

    void testMovesKeepInvariants()
    {
        std::vector<std::vector<int>> tokenRoutes;
        const Loaded loaded = makeLoaded(950, 30, 0.2, tokenRoutes);
        SearchRoutes routes(loaded.data);
        routes.load(hgs_test::individualFromTokens(loaded.data, tokenRoutes), loaded.penalties);
        const auto moves = router::hgs::makeMoves({"partial-relocate", "transfer", "split-insertion"});
        std::mt19937 rng(950);

        bool invariantsHold = true;
        bool costsDecrease = true;
        for (int trial = 0; trial < 3000; ++trial)
        {
            const std::vector<int> ids = hgs_test::sortedActiveNodes(routes);
            std::uniform_int_distribution<std::size_t> pick(0, ids.size() - 1);
            SearchNode &u = routes.node(ids[pick(rng)]);
            auto &move = *moves[static_cast<std::size_t>(trial % 3)];
            SearchNode *v = &routes.node(ids[pick(rng)]);
            if (move.scope() == router::hgs::MoveScope::Sibling)
            {
                const auto &siblings = routes.nodesOf(u.customer);
                if (siblings.size() < 2)
                {
                    continue;
                }
                v = &routes.node(siblings.front() == u.id ? siblings.back() : siblings.front());
            }
            const double before = routes.totalCost();
            if (!move.tryApply(u, *v, routes))
            {
                continue;
            }
            costsDecrease = costsDecrease && routes.totalCost() < before - router::hgs::kImprovementEpsilon / 2.0;
            invariantsHold = invariantsHold && hgs_test::invariantsHold(routes);
        }
        bool everyMoveUsed = true;
        for (const auto &move : moves)
        {
            const auto &stats = move->stats();
            everyMoveUsed = everyMoveUsed && stats.applied > 0 &&
                            stats.applied + stats.rejectedByConflict == stats.improving;
        }
        expect(everyMoveUsed, "partial relocate, transfer and split insertion must each apply and keep consistent stats");
        expect(invariantsHold, "quantity moves must keep links, summaries, quantities and pool bookkeeping valid");
        expect(costsDecrease, "every applied quantity move must strictly reduce the penalized cost");
    }

    void testSplitBeatsFixedChunks()
    {
        const router::Instance inst = hgs_test::makeClusterFarFromDepot(3, 3);
        const ProblemData data(inst, router::splitCustomers(inst));
        const Penalties penalties{1000.0, 1e6, 100.0};
        const router::hgs::Individual start = hgs_test::individualFromTokens(data, {{1}, {2}, {3}});
        router::hgs::Deadline noDeadline;

        router::hgs::LocalSearch fixed(data, router::hgs::makeMoves(kFixedMoves));
        router::hgs::Individual fixedResult = start;
        router::hgs::Rng fixedRng(1);
        fixed.run(fixedResult, penalties, fixedRng, noDeadline);

        router::hgs::LocalSearch dynamic(data, router::hgs::makeMoves(kDynamicMoves));
        dynamic.setInvariantChecks(true);
        router::hgs::Individual dynamicResult = start;
        router::hgs::Rng dynamicRng(1);
        dynamic.run(dynamicResult, penalties, dynamicRng, noDeadline);

        expect(fixedResult.cost.routes == 3 && hgs_test::near(fixedResult.cost.distance, 600.0),
               "fixed chunks of 0.6Q each must need three routes of 200");
        expect(dynamicResult.cost.routes == 2 && hgs_test::near(dynamicResult.cost.distance, 402.0) &&
                   router::hgs::isFeasible(dynamicResult.cost),
               "splitting one customer 4 + 2 must serve all three in two feasible routes of 201");
        expect(dynamicResult.giantTour.size() == 4,
               "exactly one customer must be served in two parts");
        expect(router::validate(inst, router::hgs::toSolution(data, dynamicResult)).feasible,
               "the split solution must pass the quantity-based validator");

        const router::hgs::SplitSummary splits = router::hgs::summarizeSplits(data, dynamicResult);
        expect(splits.customers == 3 && splits.deliveries == 4 && splits.splitCustomers == 1 &&
                   splits.maxDeliveriesPerCustomer == 2 && splits.deliveriesMatchingCanonical == 2 &&
                   hgs_test::near(splits.smallestPieceFraction, 2.0 / 6.0) &&
                   splits.pieceFractionHistogram == std::vector<std::size_t>({0, 0, 1, 1}),
               "the split summary must report one customer served 4 + 2 out of 6");
    }

    void testMinimumDeliveryBlocksSmallSplits()
    {
        const router::Instance inst = hgs_test::makeClusterFarFromDepot(3, 3);
        router::hgs::ProblemOptions options;
        options.minDeliveryFraction = 0.6;
        const ProblemData data(inst, router::splitCustomers(inst), options);
        expect(data.minDeliveryUnits(1) == 360, "a 60% minimum of 6 kg must be 3.6 kg");

        router::hgs::LocalSearch dynamic(data, router::hgs::makeMoves(kDynamicMoves));
        dynamic.setInvariantChecks(true);
        router::hgs::Individual result = hgs_test::individualFromTokens(data, {{1}, {2}, {3}});
        router::hgs::Rng rng(1);
        router::hgs::Deadline noDeadline;
        dynamic.run(result, {1000.0, 1e6, 100.0}, rng, noDeadline);
        expect(result.cost.routes == 3 && result.giantTour.size() == 3,
               "a minimum of 3.6 kg per delivery must forbid the 4 + 2 split");

        hgs_test::expectInvalidArgument([&]() { router::hgs::ProblemData(inst, router::splitCustomers(inst), {{}, 0.0, 20, 1.0}); },
                                        "a minimum delivery fraction of one must be rejected");
    }
}

int main()
{
    testPoolOperations();
    testPartialRelocateAgainstFullEvaluation();
    testTransferAgainstFullEvaluation();
    testSplitInsertionPlans();
    testMovesKeepInvariants();
    testSplitBeatsFixedChunks();
    testMinimumDeliveryBlocksSmallSplits();
    return hgs_test::finish("hgs quantity_moves");
}
