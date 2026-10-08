#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

#include "router/solution.hpp"

namespace router
{
    using HgsDeadline = std::optional<std::chrono::steady_clock::time_point>;

    class HgsTimeLimitReached : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    struct HgsGene
    {
        std::size_t visitIndex = 0;

        bool operator==(const HgsGene &) const = default;
    };

    struct HgsConstraintViolations
    {
        double weightExcess = 0.0;
        double volumeExcess = 0.0;
        double timeWarp = 0.0;
    };

    struct HgsPenaltyWeights
    {
        double weightPenalty = 0.0;
        double volumePenalty = 0.0;
        double timeWarpPenalty = 0.0;
    };

    struct HgsPenaltyControlOptions
    {
        std::size_t windowSize = 100;
        double targetFeasible = 0.20;
        double tolerance = 0.05;
        double increaseFactor = 1.20;
        double decreaseFactor = 0.85;
        double minPenalty = 1e-6;
        double maxPenalty = 1e12;
    };

    class HgsPenaltyController
    {
    public:
        HgsPenaltyController(
            HgsPenaltyWeights initialWeights,
            HgsPenaltyControlOptions options);

        bool observe(const HgsConstraintViolations &violations);
        const HgsPenaltyWeights &weights() const;

    private:
        HgsPenaltyWeights weights_;
        HgsPenaltyControlOptions options_;
        std::size_t observations_ = 0;
        std::size_t weightFeasible_ = 0;
        std::size_t volumeFeasible_ = 0;
        std::size_t timeFeasible_ = 0;
    };

    HgsPenaltyWeights makeInitialHgsPenalties(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsPenaltyControlOptions &controlOptions = {});

    struct HgsIndividualEvaluation
    {
        double distanceCost = 0.0;

        int routeZoneExcess = 0;
        double routeZoneCost = 0.0;
        double objectiveCost = 0.0;

        HgsConstraintViolations violations;
        double penalizedCost = 0.0;

        bool feasible = false;
    };

    struct HgsIndividual
    {
        std::vector<HgsGene> giantTour;
        Solution decodedSolution;
        HgsIndividualEvaluation evaluation;
    };

    struct HgsFitness
    {
        double diversityContribution = 0.0;
        double costRank = 0.0;
        double diversityRank = 0.0;
        double biasedFitness = 0.0;
    };

    class HgsSplitFailure : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    struct HgsRunOptions
    {
        std::uint32_t seed = 42;
        std::size_t mu = 25;
        std::size_t lambda = 40;
        std::size_t nClose = 3;
        std::size_t nElite = 8;
        std::size_t maxIterations = 20000;
        std::size_t maxNonImprovingIterations = 5000;
        std::size_t diversificationInterval = 2000;
        std::size_t maxAcceptedEducationMoves = 100;
        double repairProbability = 0.5;
        double perturbedFillFraction = 0.5;
        double perturbationStrength = 0.1;
        double fillTimeFraction = 0.2;
        HgsPenaltyControlOptions penaltyControl;
        HgsDeadline deadline;
    };

    enum class HgsStopReason
    {
        IterationLimit,
        StagnationLimit,
        TimeLimit,
        EmptyInstance
    };

    struct HgsRunResult
    {
        HgsIndividual bestFeasible;
        HgsPenaltyWeights finalPenaltyWeights;
        HgsStopReason stopReason = HgsStopReason::IterationLimit;
        std::size_t iterations = 0;
        std::size_t randomTourAttempts = 0;
        std::size_t perturbedFillAttempts = 0;
        std::size_t fillsCutByTime = 0;
        std::size_t splitFailures = 0;
        std::size_t repairsAttempted = 0;
        std::size_t repairsSucceeded = 0;
        std::size_t penaltyUpdates = 0;
        std::size_t diversifications = 0;
    };

    class HgsPopulation
    {
    public:
        explicit HgsPopulation(std::size_t visitCount);

        bool addIndividual(HgsIndividual individual);

        void updatePenaltyWeights(const HgsPenaltyWeights &penaltyWeights);

        std::size_t selectSurvivors(
            const std::vector<Visit> &visitCatalog,
            std::size_t targetSize,
            std::size_t nClose,
            std::size_t nElite,
            std::size_t triggerSize = 0,
            const HgsDeadline &deadline = std::nullopt);

        void retainBest(std::size_t targetSize);

        const HgsIndividual &selectParent(
            const std::vector<Visit> &visitCatalog,
            std::size_t nClose,
            std::size_t nElite,
            std::mt19937 &rng,
            const HgsDeadline &deadline = std::nullopt) const;

        const std::vector<HgsIndividual> &feasibleIndividuals() const;
        const std::vector<HgsIndividual> &infeasibleIndividuals() const;
        const std::optional<HgsIndividual> &bestFeasible() const;

    private:
        std::size_t visitCount_;
        std::vector<HgsIndividual> feasibleIndividuals_;
        std::vector<HgsIndividual> infeasibleIndividuals_;
        std::optional<HgsIndividual> bestFeasible_;
    };

    struct HgsRouteSegmentEvaluation
    {
        Route route;
        double distanceCost = 0.0;
        HgsConstraintViolations violations;
        int routeZoneExcess = 0;
        bool hasCustomerConflict = false;
    };

    std::vector<HgsFitness> computeHgsFitness(
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsIndividual> &individuals,
        std::size_t nClose,
        std::size_t nElite,
        const HgsDeadline &deadline = std::nullopt);

    double directedBrokenPairsDistance(
        const std::vector<Visit> &visitCatalog,
        const Solution &first,
        const Solution &second,
        const HgsDeadline &deadline = std::nullopt);

    std::vector<HgsGene> makeCanonicalGiantTour(
        std::size_t visitCount);

    std::vector<HgsGene> orderedCrossover(
        const std::vector<HgsGene> &firstParent,
        const std::vector<HgsGene> &secondParent,
        std::size_t beginIndex,
        std::size_t endIndex);

    std::vector<HgsGene> orderedCrossover(
        const std::vector<HgsGene> &firstParent,
        const std::vector<HgsGene> &secondParent,
        std::mt19937 &rng);

    std::vector<HgsGene> encodeSolutionAsGiantTour(
        const Solution &solution,
        const std::vector<Visit> &visitCatalog);

    HgsRouteSegmentEvaluation evaluateGiantTourSegment(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        std::size_t beginIndex,
        std::size_t endIndex);

    HgsRouteSegmentEvaluation evaluateGiantTourSegment(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        std::size_t beginIndex,
        std::size_t endIndex,
        const std::vector<int> &zoneOf);

    HgsIndividualEvaluation aggregateHgsIndividualEvaluation(
        const std::vector<HgsRouteSegmentEvaluation>
            &routeEvaluations,
        const HgsPenaltyWeights &penaltyWeights,
        double zonePenalty);

    HgsIndividual makeHgsIndividualFromSolution(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const Solution &solution,
        const HgsPenaltyWeights &penaltyWeights);

    HgsIndividual makeHgsIndividualFromSolution(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const Solution &solution,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty);

    HgsIndividual educateHgsRelocate(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        std::size_t maxAcceptedMoves,
        const HgsDeadline &deadline = std::nullopt);

    HgsIndividual educateHgsRelocate(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty,
        std::size_t maxAcceptedMoves,
        const HgsDeadline &deadline = std::nullopt);

    HgsIndividual educateHgsIndividual(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        std::size_t maxAcceptedMoves,
        const HgsDeadline &deadline = std::nullopt);

    HgsIndividual educateHgsIndividual(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty,
        std::size_t maxAcceptedMoves,
        const HgsDeadline &deadline = std::nullopt);

    HgsIndividual repairHgsIndividual(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        std::size_t maxAcceptedMovesPerAttempt,
        const HgsDeadline &deadline = std::nullopt);

    HgsIndividual repairHgsIndividual(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty,
        std::size_t maxAcceptedMovesPerAttempt,
        const HgsDeadline &deadline = std::nullopt);

    HgsIndividual decodeGiantTour(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        const HgsPenaltyWeights &penaltyWeights,
        const HgsDeadline &deadline = std::nullopt);

    HgsIndividual decodeGiantTour(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty,
        const HgsDeadline &deadline = std::nullopt);

    HgsRunResult runHgs(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const Solution &feasibleSeed,
        const HgsPenaltyWeights &initialPenalties,
        const HgsRunOptions &options);

    HgsRunResult runHgs(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const Solution &feasibleSeed,
        const HgsPenaltyWeights &initialPenalties,
        const HgsRunOptions &options,
        const std::vector<int> &zoneOf,
        double zonePenalty);

    bool isCompleteGiantTourPermutation(
        const std::vector<HgsGene> &giantTour,
        std::size_t visitCount);
}
