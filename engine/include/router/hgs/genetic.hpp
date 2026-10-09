#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/local_search.hpp"
#include "router/hgs/moves.hpp"
#include "router/hgs/penalty_controller.hpp"
#include "router/hgs/population.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/search_context.hpp"

namespace router::hgs
{
    struct GeneticConfig
    {
        std::uint32_t seed = 42;
        PopulationOptions population;
        std::vector<std::string> moves{"relocate", "swap", "2-opt*"};
        std::size_t maxIterations = 20000;
        std::size_t maxNonImprovingIterations = 5000;
        std::size_t diversificationInterval = 2000;
        double repairProbability = 0.5;
        double repairPenaltyFactor = 10.0;
        double perturbedFillFraction = 0.5;
        double perturbationStrength = 0.1;
        double fillTimeFraction = 0.2;
        PenaltyControlOptions penaltyControl;
        bool checkInvariants = false;
        std::size_t traceInterval = 0;
    };

    enum class StopReason
    {
        IterationLimit,
        StagnationLimit,
        TimeLimit,
        EmptyInstance
    };

    const char *stopReasonName(StopReason reason);

    struct GeneticStats
    {
        std::size_t iterations = 0;
        std::size_t iterationOfBest = 0;
        double msToBest = 0.0;
        std::size_t randomFillAttempts = 0;
        std::size_t perturbedFillAttempts = 0;
        std::size_t fillsCutByTime = 0;
        std::size_t splitFailures = 0;
        std::size_t repairsAttempted = 0;
        std::size_t repairsSucceeded = 0;
        std::size_t penaltyUpdates = 0;
        std::size_t diversifications = 0;
        StageStats fill;
        StageStats parentSelection;
        StageStats crossover;
        StageStats split;
        StageStats education;
        StageStats repair;
        StageStats populationUpdate;
    };

    struct TraceRecord
    {
        std::size_t iteration = 0;
        double elapsedMs = 0.0;
        std::optional<double> bestObjective;
        std::size_t feasibleSize = 0;
        std::size_t infeasibleSize = 0;
        Penalties penalties;
    };

    using TraceSink = std::function<void(const TraceRecord &)>;

    struct GeneticResult
    {
        Individual best;
        Penalties initialPenalties;
        Penalties finalPenalties;
        StopReason stopReason = StopReason::IterationLimit;
        GeneticStats stats;
        LocalSearchStats localSearch;
        std::vector<std::pair<std::string, MoveStats>> moves;
        PopulationStats population;
    };

    void validateGeneticConfig(const GeneticConfig &config);

    GeneticResult runGenetic(
        const ProblemData &data,
        const Individual &feasibleSeed,
        const GeneticConfig &config,
        Deadline deadline,
        const TraceSink &trace = {});
}
