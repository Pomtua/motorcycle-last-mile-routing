#include <algorithm>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/population.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/split.hpp"

using hgs_test::expect;
using hgs_test::near;
using router::hgs::Individual;
using router::hgs::Penalties;
using router::hgs::Population;
using router::hgs::PopulationOptions;
using router::hgs::ProblemData;

namespace
{
    struct Setup
    {
        router::Instance inst;
        std::vector<router::Visit> catalog;
    };

    Setup makeSetup(std::uint32_t seed, int customers, bool wideWindows)
    {
        Setup setup;
        hgs_test::RandomInstanceOptions options{.customers = customers};
        if (wideWindows)
        {
            options.horizon = 1e6;
            options.minWindow = 1e6;
            options.maxWindow = 1e6;
        }
        setup.inst = hgs_test::makeRandomInstance(seed, options);
        setup.catalog = router::splitCustomers(setup.inst);
        return setup;
    }

    std::vector<Individual> randomIndividuals(const ProblemData &data, const Setup &setup, std::mt19937 &rng, int count, double newRouteProbability)
    {
        std::vector<Individual> individuals;
        for (int index = 0; index < count; ++index)
        {
            individuals.push_back(router::hgs::makeIndividual(
                data, hgs_test::makeConflictFreeRoutes(rng, setup.catalog, newRouteProbability)));
        }
        return individuals;
    }

    std::vector<std::vector<int>> capacityPackedRoutes(const ProblemData &data, std::mt19937 &rng)
    {
        std::vector<int> order(data.visitCount());
        std::iota(order.begin(), order.end(), 1);
        std::shuffle(order.begin(), order.end(), rng);

        std::vector<std::vector<int>> routes;
        std::vector<double> weights;
        std::vector<double> volumes;
        for (int visit : order)
        {
            const auto &chunk = data.visit(visit);
            bool placed = false;
            for (std::size_t index = 0; index < routes.size() && !placed; ++index)
            {
                const bool fits = weights[index] + chunk.weight <= data.weightCapacity() &&
                                  volumes[index] + chunk.volume <= data.volumeCapacity();
                const bool sameCustomer = std::any_of(routes[index].begin(), routes[index].end(), [&](int other)
                                                      { return data.visit(other).node == chunk.node; });
                if (fits && !sameCustomer)
                {
                    routes[index].push_back(visit);
                    weights[index] += chunk.weight;
                    volumes[index] += chunk.volume;
                    placed = true;
                }
            }
            if (!placed)
            {
                routes.push_back({visit});
                weights.push_back(chunk.weight);
                volumes.push_back(chunk.volume);
            }
        }
        return routes;
    }

    void testBrokenPairs()
    {
        const Setup setup = makeSetup(700, 30, false);
        const ProblemData data(setup.inst, setup.catalog);
        std::mt19937 rng(700);
        const auto individuals = randomIndividuals(data, setup, rng, 12, 0.2);

        bool symmetric = true;
        bool zeroOnSelf = true;
        bool bounded = true;
        for (const auto &first : individuals)
        {
            zeroOnSelf = zeroOnSelf && router::hgs::brokenPairsDistance(first, first) == 0.0;
            for (const auto &second : individuals)
            {
                const double distance = router::hgs::brokenPairsDistance(first, second);
                symmetric = symmetric && distance == router::hgs::brokenPairsDistance(second, first);
                bounded = bounded && distance >= 0.0 && distance <= 1.0;
            }
        }
        expect(symmetric && zeroOnSelf && bounded,
               "broken-pairs distance must be symmetric, zero on identical individuals and within [0, 1]");

        const router::Instance small = hgs_test::makeRandomInstance(701, {.customers = 3, .maxDemandRatio = 0.4});
        const ProblemData smallData(small, router::splitCustomers(small));
        expect(smallData.visitCount() == 3, "the hand-worked example needs three unsplit visits");
        const Individual forward = router::hgs::makeIndividual(smallData, {{1, 2, 3}});
        const Individual reordered = router::hgs::makeIndividual(smallData, {{1, 3, 2}});
        expect(near(router::hgs::brokenPairsDistance(forward, reordered), 5.0 / 6.0),
               "routes 1-2-3 and 1-3-2 must differ in five of six predecessor and successor links");
    }

    void testRanks()
    {
        const auto ascending = router::hgs::averageRanks({30.0, 10.0, 20.0, 10.0}, false);
        expect(ascending == std::vector<double>({1.0, 0.375, 0.75, 0.375}), "ties must share the average normalized rank");
        const auto descending = router::hgs::averageRanks({0.1, 0.5, 0.3}, true);
        expect(near(descending[1], 1.0 / 3.0) && near(descending[0], 1.0), "higher-is-better ranks must favour larger values");
    }

    void testDistanceCacheAndSurvivors()
    {
        const Setup setup = makeSetup(720, 25, false);
        const ProblemData data(setup.inst, setup.catalog);
        const Penalties penalties{10.0, 2000.0, 0.5};
        const PopulationOptions options{.mu = 6, .lambda = 4, .nClose = 2, .nElite = 2};
        Population population(data, options, penalties);
        std::mt19937 rng(720);

        auto individuals = randomIndividuals(data, setup, rng, 30, 0.2);
        const Individual duplicate = individuals.front();
        individuals.insert(individuals.begin(), 2, duplicate);
        bool boundedSize = true;
        for (auto &individual : individuals)
        {
            population.add(individual);
            boundedSize = boundedSize && population.infeasible().size() <= options.mu + options.lambda;
        }

        const auto &subpopulation = population.infeasible();
        bool cacheFresh = true;
        bool noClones = true;
        for (std::size_t first = 0; first < subpopulation.size(); ++first)
        {
            for (std::size_t second = 0; second < subpopulation.size(); ++second)
            {
                const double fresh = router::hgs::brokenPairsDistance(subpopulation.member(first), subpopulation.member(second));
                cacheFresh = cacheFresh && subpopulation.distance(first, second) == fresh;
                noClones = noClones && (first == second || fresh > router::hgs::kCloneDistance);
            }
        }
        const auto &stats = population.stats();
        expect(boundedSize, "a subpopulation must never exceed mu + lambda");
        expect(stats.survivorSelections > 0 && subpopulation.size() >= options.mu, "survivor selection must trim back to mu");
        expect(cacheFresh, "cached distances must stay equal to fresh broken-pairs distances after removals");
        expect(stats.removedClones > 0 && noClones, "clones must be removed before diverse members");
        expect(stats.distancesComputed > 0 && stats.added == individuals.size(), "population stats must count additions and distances");
    }

    void testBestFeasibleTracking()
    {
        const Setup setup = makeSetup(730, 20, true);
        const ProblemData data(setup.inst, setup.catalog);
        const Penalties penalties{10.0, 2000.0, 0.5};
        Population population(data, {}, penalties);
        std::mt19937 rng(730);

        std::vector<Individual> feasible;
        std::set<double> distinctObjectives;
        for (int trial = 0; trial < 40; ++trial)
        {
            Individual individual = router::hgs::makeIndividual(data, capacityPackedRoutes(data, rng));
            if (router::hgs::isFeasible(individual.cost))
            {
                distinctObjectives.insert(router::hgs::objectiveCost(data, individual.cost));
                feasible.push_back(std::move(individual));
            }
        }
        expect(feasible.size() == 40 && distinctObjectives.size() >= 5,
               "capacity-packed routes under wide windows must be feasible with varied costs");

        bool reportsMatch = true;
        double best = std::numeric_limits<double>::infinity();
        for (const auto &individual : feasible)
        {
            const double objective = router::hgs::objectiveCost(data, individual.cost);
            const bool expected = objective < best;
            best = std::min(best, objective);
            reportsMatch = reportsMatch && population.add(individual) == expected;
        }
        expect(reportsMatch, "add must report exactly the strict improvements of the best feasible objective");
        expect(population.bestFeasible() && near(router::hgs::objectiveCost(data, population.bestFeasible()->cost), best),
               "the best feasible individual must be kept even if trimmed from the subpopulation");
    }

    void testPenaltiesRetentionAndParents()
    {
        const Setup setup = makeSetup(740, 25, false);
        const ProblemData data(setup.inst, setup.catalog);
        Population population(data, {.mu = 25, .lambda = 40, .nClose = 3, .nElite = 4}, {10.0, 2000.0, 0.5});
        std::mt19937 rng(740);
        for (auto &individual : randomIndividuals(data, setup, rng, 20, 0.3))
        {
            population.add(individual);
        }

        router::hgs::Rng first(9);
        router::hgs::Rng second(9);
        const auto parents = population.selectParents(first);
        const auto repeated = population.selectParents(second);
        expect(parents == repeated, "parent selection must be reproducible with the same seed");

        const Penalties heavyTime{10.0, 2000.0, 500.0};
        population.setPenalties(heavyTime);
        std::vector<double> costs;
        for (std::size_t index = 0; index < population.infeasible().size(); ++index)
        {
            costs.push_back(router::hgs::penalizedCost(data, population.infeasible().member(index).cost, heavyTime));
        }
        std::sort(costs.begin(), costs.end());
        population.retainBest(4);
        std::vector<double> kept;
        for (std::size_t index = 0; index < population.infeasible().size(); ++index)
        {
            kept.push_back(router::hgs::penalizedCost(data, population.infeasible().member(index).cost, heavyTime));
        }
        std::sort(kept.begin(), kept.end());
        expect(kept == std::vector<double>(costs.begin(), costs.begin() + 4),
               "retention must keep the cheapest members under the current penalties");

        hgs_test::expectInvalidArgument([&]() { population.retainBest(0); }, "retaining nobody must be rejected");
        hgs_test::expectInvalidArgument([&]() { Population bad(data, {.mu = 0}, heavyTime); }, "zero mu must be rejected");
        hgs_test::expectInvalidArgument([&]() { Population bad(data, {.mu = 3, .nElite = 4}, heavyTime); }, "nElite above mu must be rejected");
    }
}

int main()
{
    testBrokenPairs();
    testRanks();
    testDistanceCacheAndSurvivors();
    testBestFeasibleTracking();
    testPenaltiesRetentionAndParents();
    return hgs_test::finish("hgs population");
}
