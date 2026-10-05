#pragma once

#include <cstddef>
#include <vector>

#include "router/solution.hpp"

namespace router
{
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

    struct HgsRouteSegmentEvaluation
    {
        Route route;
        double distanceCost = 0.0;
        HgsConstraintViolations violations;
        int routeZoneExcess = 0;
        bool hasCustomerConflict = false;
    };

    std::vector<HgsGene> makeCanonicalGiantTour(
        std::size_t visitCount);

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

    HgsIndividual decodeGiantTour(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        const HgsPenaltyWeights &penaltyWeights);

    HgsIndividual decodeGiantTour(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty);

    bool isCompleteGiantTourPermutation(
        const std::vector<HgsGene> &giantTour,
        std::size_t visitCount);
}
