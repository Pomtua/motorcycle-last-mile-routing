#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/genetic.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/search_context.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"

using hgs_test::expect;
using router::hgs::Deadline;
using router::hgs::GeneticConfig;
using router::hgs::GeneticResult;
using router::hgs::Individual;
using router::hgs::ProblemData;
using router::hgs::StopReason;

namespace
{
    struct Scenario
    {
        router::Instance inst;
        std::vector<router::Visit> catalog;
        std::vector<int> zoneOf;
    };

    Scenario makeScenario(std::uint32_t seed, int customers, int zones = 0)
    {
        Scenario scenario;
        scenario.inst = hgs_test::makeRandomInstance(seed, {.customers = customers, .minWindow = 3600.0, .maxWindow = 14400.0});
        scenario.catalog = router::splitCustomers(scenario.inst);
        if (zones > 0)
        {
            scenario.zoneOf = hgs_test::makeRandomZones(seed, scenario.inst, zones);
        }
        return scenario;
    }

    Individual singletonSeed(const ProblemData &data)
    {
        std::vector<std::vector<int>> routes;
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            routes.push_back({visit});
        }
        return router::hgs::makeIndividual(data, routes);
    }

    GeneticConfig smallConfig()
    {
        GeneticConfig config;
        config.population = {.mu = 8, .lambda = 10, .nClose = 3, .nElite = 2};
        config.maxIterations = 150;
        config.maxNonImprovingIterations = 1000;
        config.diversificationInterval = 60;
        config.penaltyControl.windowSize = 20;
        return config;
    }

    bool validFor(const Scenario &scenario, const ProblemData &data, const Individual &individual)
    {
        return router::validate(scenario.inst, router::hgs::toSolution(data, individual)).feasible;
    }

    void testImprovesSeedReproducibly()
    {
        const Scenario scenario = makeScenario(800, 40);
        const ProblemData data(scenario.inst, scenario.catalog);
        const Individual seed = singletonSeed(data);
        expect(router::hgs::isFeasible(seed.cost), "singleton routes under wide windows must form a feasible seed");

        GeneticConfig config = smallConfig();
        config.checkInvariants = true;
        const GeneticResult first = router::hgs::runGenetic(data, seed, config, Deadline{});
        const GeneticResult second = router::hgs::runGenetic(data, seed, config, Deadline{});

        expect(first.stopReason == StopReason::IterationLimit && first.stats.iterations == config.maxIterations,
               "an unlimited deadline must run to the iteration limit");
        expect(router::hgs::isFeasible(first.best.cost) && validFor(scenario, data, first.best),
               "the returned best individual must pass the independent validator");
        expect(router::hgs::objectiveCost(data, first.best.cost) < router::hgs::objectiveCost(data, seed.cost),
               "genetic search must improve a singleton-route seed");
        expect(first.best.routes == second.best.routes && first.stats.iterationOfBest == second.stats.iterationOfBest,
               "genetic search must be reproducible with the same seed and iteration limit");

        const std::size_t attempts = 4 * config.population.mu;
        expect(first.stats.perturbedFillAttempts + first.stats.randomFillAttempts ==
                       attempts * (1 + first.stats.diversifications) &&
                   first.stats.perturbedFillAttempts == (attempts / 2) * (1 + first.stats.diversifications),
               "each fill must split its attempts between perturbed and random tours");
        expect(first.stats.education.calls > first.stats.iterations && first.localSearch.runs > 0,
               "every split child must be educated");
        expect(first.moves.size() == 3 && first.population.added > 0, "move and population statistics must be reported");
        expect(first.stats.penaltyUpdates > 0, "a short penalty window must trigger penalty updates");
    }

    void testStopReasons()
    {
        const Scenario scenario = makeScenario(810, 25);
        const ProblemData data(scenario.inst, scenario.catalog);
        const Individual seed = singletonSeed(data);

        GeneticConfig stagnating = smallConfig();
        stagnating.maxNonImprovingIterations = 5;
        stagnating.diversificationInterval = 0;
        const auto stagnant = router::hgs::runGenetic(data, seed, stagnating, Deadline{});
        expect(stagnant.stopReason == StopReason::StagnationLimit || stagnant.stopReason == StopReason::IterationLimit,
               "a short stagnation budget must stop the search early");
        expect(stagnant.stats.iterations < stagnating.maxIterations || stagnant.stopReason == StopReason::IterationLimit,
               "stagnation must end the loop before the iteration limit");

        const Deadline expired(router::hgs::SteadyClock::now() - std::chrono::seconds(1));
        const auto timedOut = router::hgs::runGenetic(data, seed, smallConfig(), expired);
        expect(timedOut.stopReason == StopReason::TimeLimit && timedOut.stats.iterations == 0 &&
                   timedOut.best.routes == seed.routes,
               "an expired deadline must return the seed without searching");

        router::Instance empty;
        empty.horizon = 100.0;
        empty.fleet = {0, 1.0, 1.0};
        empty.nodes.resize(1);
        empty.distanceMatrix = {{0.0}};
        empty.durationMatrix = {{0.0}};
        const ProblemData emptyData(empty, {});
        const auto emptyRun = router::hgs::runGenetic(emptyData, router::hgs::makeIndividual(emptyData, {}), smallConfig(), Deadline{});
        expect(emptyRun.stopReason == StopReason::EmptyInstance && emptyRun.best.routes.empty(),
               "an instance without customers must stop immediately");
    }

    void testFillTimeCapWithFakeClock()
    {
        const Scenario scenario = makeScenario(820, 25);
        const ProblemData data(scenario.inst, scenario.catalog);
        const Individual seed = singletonSeed(data);

        auto now = std::make_shared<router::hgs::SteadyClock::time_point>(router::hgs::SteadyClock::time_point{});
        const auto tickingClock = [now]()
        {
            *now += std::chrono::milliseconds(1);
            return *now;
        };
        const Deadline deadline(*now + std::chrono::seconds(4), tickingClock);

        GeneticConfig config = smallConfig();
        config.population.mu = 200;
        config.population.lambda = 40;
        config.maxIterations = 1000000;
        config.diversificationInterval = 0;
        const auto result = router::hgs::runGenetic(data, seed, config, deadline);
        expect(result.stats.fillsCutByTime == 1, "the initial fill must stop at its share of the time budget");
        expect(result.stats.iterations > 0, "crossover iterations must run after a time-capped fill");
        expect(result.stopReason == StopReason::TimeLimit, "the search must stop at the deadline");
        expect(validFor(scenario, data, result.best), "a time-limited search must still return a valid incumbent");
    }

    void testZonesAndTrace()
    {
        const Scenario scenario = makeScenario(830, 30, 4);
        const ProblemData data(scenario.inst, scenario.catalog, {scenario.zoneOf, 500.0, 20});
        const Individual seed = singletonSeed(data);

        GeneticConfig config = smallConfig();
        config.maxIterations = 50;
        config.traceInterval = 10;
        std::vector<router::hgs::TraceRecord> records;
        const auto result = router::hgs::runGenetic(
            data, seed, config, Deadline{}, [&](const router::hgs::TraceRecord &record) { records.push_back(record); });

        bool ordered = !records.empty();
        for (std::size_t index = 1; index < records.size(); ++index)
        {
            ordered = ordered && records[index].iteration >= records[index - 1].iteration &&
                      records[index].bestObjective.has_value() &&
                      *records[index].bestObjective <= *records[index - 1].bestObjective + 1e-9;
        }
        expect(records.size() == 6 && ordered, "trace must report every interval plus a final record with non-increasing best cost");
        expect(hgs_test::near(router::hgs::objectiveCost(data, result.best.cost),
                              result.best.cost.distance + 500.0 * result.best.cost.zoneExcess),
               "the zoned objective must add the penalty per extra zone");
    }

    void testValidation()
    {
        const Scenario scenario = makeScenario(840, 10);
        const ProblemData data(scenario.inst, scenario.catalog);
        const Individual seed = singletonSeed(data);
        const auto rejects = [&](auto mutate, const char *message)
        {
            GeneticConfig config = smallConfig();
            mutate(config);
            hgs_test::expectInvalidArgument([&]() { router::hgs::runGenetic(data, seed, config, Deadline{}); }, message);
        };
        rejects([](auto &c) { c.repairProbability = 1.5; }, "repair probability above one must be rejected");
        rejects([](auto &c) { c.repairPenaltyFactor = 0.5; }, "repair factors below one must be rejected");
        rejects([](auto &c) { c.fillTimeFraction = 0.0; }, "a zero fill time share must be rejected");
        rejects([](auto &c) { c.maxNonImprovingIterations = 0; }, "a zero stagnation budget must be rejected");
        rejects([](auto &c) { c.moves = {"or-opt"}; }, "unknown moves must be rejected");
        rejects([](auto &c) { c.population.mu = 0; }, "invalid population options must be rejected");

        Individual infeasible = seed;
        infeasible.cost.timeWarp = 10.0;
        hgs_test::expectInvalidArgument([&]() { router::hgs::runGenetic(data, infeasible, smallConfig(), Deadline{}); },
                                        "an infeasible seed must be rejected");
    }
}

int main()
{
    testImprovesSeedReproducibly();
    testStopReasons();
    testFillTimeCapWithFakeClock();
    testZonesAndTrace();
    testValidation();
    return hgs_test::finish("hgs genetic");
}
