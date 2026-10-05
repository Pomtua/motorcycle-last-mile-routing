#include "router/hgs.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace router
{
    namespace
    {
        constexpr double kWeightTolerance = 1e-6;
        constexpr double kVolumeTolerance = 1e-9;
        constexpr double kTimeTolerance = 1e-6;

        struct HgsSplitArc
        {
            std::size_t endIndex = 0;
            double penalizedCost = 0.0;
        };

        void validatePenaltyWeights(
            const HgsPenaltyWeights &penaltyWeights,
            double zonePenalty)
        {
            if (penaltyWeights.weightPenalty < 0.0 ||
                penaltyWeights.volumePenalty < 0.0 ||
                penaltyWeights.timeWarpPenalty < 0.0 ||
                zonePenalty < 0.0)
            {
                throw std::invalid_argument(
                    "HGS penalty weights must be non-negative");
            }
        }

        double computePenalizedCost(
            double distanceCost,
            int routeZoneExcess,
            const HgsConstraintViolations &violations,
            const HgsPenaltyWeights &penaltyWeights,
            double zonePenalty)
        {
            return distanceCost +
                   zonePenalty *
                       static_cast<double>(
                           routeZoneExcess) +
                   penaltyWeights.weightPenalty *
                       violations.weightExcess +
                   penaltyWeights.volumePenalty *
                       violations.volumeExcess +
                   penaltyWeights.timeWarpPenalty *
                       violations.timeWarp;
        }

        std::vector<std::vector<HgsSplitArc>>
        buildSplitArcs(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const std::vector<HgsGene> &giantTour,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty)
        {
            std::vector<std::vector<HgsSplitArc>>
                arcsByStart(giantTour.size());

            for (std::size_t beginIndex = 0;
                 beginIndex < giantTour.size();
                 ++beginIndex)
            {
                for (std::size_t endIndex =
                         beginIndex + 1;
                     endIndex <= giantTour.size();
                     ++endIndex)
                {
                    const HgsRouteSegmentEvaluation
                        routeEvaluation =
                            zoneOf == nullptr
                                ? evaluateGiantTourSegment(
                                      inst,
                                      visitCatalog,
                                      giantTour,
                                      beginIndex,
                                      endIndex)
                                : evaluateGiantTourSegment(
                                      inst,
                                      visitCatalog,
                                      giantTour,
                                      beginIndex,
                                      endIndex,
                                      *zoneOf);

                    if (routeEvaluation.hasCustomerConflict)
                    {
                        break;
                    }

                    if (routeEvaluation
                                .violations.weightExcess >
                            inst.fleet.weightCapacity +
                                kWeightTolerance ||
                        routeEvaluation
                                .violations.volumeExcess >
                            inst.fleet.volumeCapacity +
                                kVolumeTolerance)
                    {
                        break;
                    }

                    arcsByStart[beginIndex].push_back(
                        {endIndex,
                         computePenalizedCost(
                             routeEvaluation.distanceCost,
                             routeEvaluation.routeZoneExcess,
                             routeEvaluation.violations,
                             penaltyWeights,
                             zonePenalty)});
                }
            }

            return arcsByStart;
        }

        HgsIndividual decodeGiantTourImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const std::vector<HgsGene> &giantTour,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty)
        {
            validatePenaltyWeights(
                penaltyWeights, zonePenalty);

            if (!isCompleteGiantTourPermutation(
                    giantTour, visitCatalog.size()))
            {
                throw std::invalid_argument(
                    "cannot decode an incomplete HGS "
                    "giant-tour permutation");
            }

            HgsIndividual individual;
            individual.giantTour = giantTour;

            if (giantTour.empty())
            {
                individual.evaluation =
                    aggregateHgsIndividualEvaluation(
                        {},
                        penaltyWeights,
                        zonePenalty);
                return individual;
            }

            if (inst.fleet.size <= 0)
            {
                throw std::invalid_argument(
                    "HGS decoding requires a positive fleet size");
            }

            const std::size_t visitCount =
                giantTour.size();
            const std::size_t maxRoutes =
                std::min(
                    visitCount,
                    static_cast<std::size_t>(
                        inst.fleet.size));
            const double infinity =
                std::numeric_limits<double>::infinity();
            const std::size_t noParent =
                visitCount + 1;

            const std::vector<std::vector<HgsSplitArc>>
                arcsByStart =
                    buildSplitArcs(
                        inst,
                        visitCatalog,
                        giantTour,
                        penaltyWeights,
                        zoneOf,
                        zonePenalty);

            std::vector<std::vector<std::size_t>> parent(
                maxRoutes + 1,
                std::vector<std::size_t>(
                    visitCount + 1,
                    noParent));
            std::vector<double> previous(
                visitCount + 1,
                infinity);
            std::vector<double> current(
                visitCount + 1,
                infinity);

            previous[0] = 0.0;

            double bestCost = infinity;
            std::size_t bestRouteCount = 0;

            for (std::size_t routeCount = 1;
                 routeCount <= maxRoutes;
                 ++routeCount)
            {
                std::fill(
                    current.begin(),
                    current.end(),
                    infinity);

                for (std::size_t beginIndex = 0;
                     beginIndex < visitCount;
                     ++beginIndex)
                {
                    if (!std::isfinite(
                            previous[beginIndex]))
                    {
                        continue;
                    }

                    for (const HgsSplitArc &arc :
                         arcsByStart[beginIndex])
                    {
                        const double candidateCost =
                            previous[beginIndex] +
                            arc.penalizedCost;

                        if (candidateCost <
                            current[arc.endIndex])
                        {
                            current[arc.endIndex] =
                                candidateCost;
                            parent[routeCount]
                                  [arc.endIndex] =
                                beginIndex;
                        }
                    }
                }

                if (current[visitCount] < bestCost)
                {
                    bestCost = current[visitCount];
                    bestRouteCount = routeCount;
                }

                previous.swap(current);
            }

            if (bestRouteCount == 0)
            {
                throw std::runtime_error(
                    "HGS Split found no hard-valid "
                    "partition within fleet size");
            }

            std::vector<std::pair<
                std::size_t,
                std::size_t>>
                segments;
            std::size_t endIndex = visitCount;

            for (std::size_t routeCount =
                     bestRouteCount;
                 routeCount > 0;
                 --routeCount)
            {
                const std::size_t beginIndex =
                    parent[routeCount][endIndex];

                if (beginIndex == noParent)
                {
                    throw std::logic_error(
                        "HGS Split parent chain is incomplete");
                }

                segments.push_back(
                    {beginIndex, endIndex});
                endIndex = beginIndex;
            }

            if (endIndex != 0)
            {
                throw std::logic_error(
                    "HGS Split parent chain does not "
                    "reach the giant-tour start");
            }

            std::reverse(
                segments.begin(),
                segments.end());

            std::vector<HgsRouteSegmentEvaluation>
                routeEvaluations;
            routeEvaluations.reserve(segments.size());
            individual.decodedSolution.routes.reserve(
                segments.size());

            for (const auto [begin, end] : segments)
            {
                HgsRouteSegmentEvaluation routeEvaluation =
                    zoneOf == nullptr
                        ? evaluateGiantTourSegment(
                              inst,
                              visitCatalog,
                              giantTour,
                              begin,
                              end)
                        : evaluateGiantTourSegment(
                              inst,
                              visitCatalog,
                              giantTour,
                              begin,
                              end,
                              *zoneOf);

                individual.decodedSolution.routes.push_back(
                    routeEvaluation.route);
                routeEvaluations.push_back(
                    std::move(routeEvaluation));
            }

            individual.evaluation =
                aggregateHgsIndividualEvaluation(
                    routeEvaluations,
                    penaltyWeights,
                    zonePenalty);

            return individual;
        }

        double excessAbove(
            double value,
            double limit,
            double tolerance)
        {
            if (value <= limit + tolerance)
            {
                return 0.0;
            }

            return value - limit;
        }

        HgsRouteSegmentEvaluation evaluateGiantTourSegmentImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const std::vector<HgsGene> &giantTour,
            std::size_t beginIndex,
            std::size_t endIndex,
            const std::vector<int> *zoneOf)
        {
            if (beginIndex >= endIndex ||
                endIndex > giantTour.size())
            {
                throw std::invalid_argument(
                    "HGS route segment must be a non-empty "
                    "half-open range inside the giant tour");
            }

            if (zoneOf != nullptr &&
                zoneOf->size() != inst.nodes.size())
            {
                throw std::invalid_argument(
                    "zone assignment size must match "
                    "instance node count");
            }

            HgsRouteSegmentEvaluation evaluation;
            evaluation.route.stops.reserve(
                endIndex - beginIndex);

            std::set<int> customers;
            std::set<int> usedZones;

            double totalWeight = 0.0;
            double totalVolume = 0.0;
            double clock = 0.0;
            int currentNode = 0;

            for (std::size_t position = beginIndex;
                 position < endIndex;
                 ++position)
            {
                const HgsGene gene = giantTour[position];

                if (gene.visitIndex >= visitCatalog.size())
                {
                    throw std::invalid_argument(
                        "HGS gene visit index is outside "
                        "the visit catalog");
                }

                const Visit &visit =
                    visitCatalog[gene.visitIndex];

                if (visit.nodeIndex <= 0 ||
                    visit.nodeIndex >=
                        static_cast<int>(inst.nodes.size()))
                {
                    throw std::invalid_argument(
                        "HGS visit contains an invalid "
                        "customer node index");
                }

                evaluation.route.stops.push_back(visit);

                if (!customers.insert(
                        visit.nodeIndex).second)
                {
                    evaluation.hasCustomerConflict = true;
                }

                if (zoneOf != nullptr)
                {
                    const int zone =
                        (*zoneOf)[static_cast<std::size_t>(
                            visit.nodeIndex)];

                    if (zone < 0)
                    {
                        throw std::invalid_argument(
                            "customer must have a "
                            "non-negative zone assignment");
                    }

                    usedZones.insert(zone);
                }

                totalWeight += visit.weight;
                totalVolume += visit.volume;

                evaluation.distanceCost +=
                    inst.distanceMatrix[
                        static_cast<std::size_t>(
                            currentNode)]
                        [static_cast<std::size_t>(
                            visit.nodeIndex)];

                double arrival =
                    clock +
                    inst.durationMatrix[
                        static_cast<std::size_t>(
                            currentNode)]
                        [static_cast<std::size_t>(
                            visit.nodeIndex)];

                const Node &node =
                    inst.nodes[static_cast<std::size_t>(
                        visit.nodeIndex)];

                if (arrival < node.twStart)
                {
                    arrival = node.twStart;
                }
                else if (arrival > node.twEnd)
                {
                    if (arrival >
                        node.twEnd + kTimeTolerance)
                    {
                        evaluation.violations.timeWarp +=
                            arrival - node.twEnd;
                    }

                    arrival = node.twEnd;
                }

                clock = arrival + node.serviceTime;
                currentNode = visit.nodeIndex;
            }

            evaluation.distanceCost +=
                inst.distanceMatrix[
                    static_cast<std::size_t>(currentNode)][0];

            const double finishTime =
                clock +
                inst.durationMatrix[
                    static_cast<std::size_t>(currentNode)][0];

            if (finishTime >
                inst.horizon + kTimeTolerance)
            {
                evaluation.violations.timeWarp +=
                    finishTime - inst.horizon;
            }

            evaluation.violations.weightExcess =
                excessAbove(
                    totalWeight,
                    inst.fleet.weightCapacity,
                    kWeightTolerance);
            evaluation.violations.volumeExcess =
                excessAbove(
                    totalVolume,
                    inst.fleet.volumeCapacity,
                    kVolumeTolerance);

            if (!usedZones.empty())
            {
                evaluation.routeZoneExcess =
                    static_cast<int>(
                        usedZones.size()) -
                    1;
            }

            return evaluation;
        }
    }

    std::vector<HgsGene> makeCanonicalGiantTour(
        std::size_t visitCount)
    {
        std::vector<HgsGene> giantTour;
        giantTour.reserve(visitCount);

        for (std::size_t visitIndex = 0;
             visitIndex < visitCount;
             ++visitIndex)
        {
            giantTour.push_back({visitIndex});
        }

        return giantTour;
    }

    std::vector<HgsGene> encodeSolutionAsGiantTour(
        const Solution &solution,
        const std::vector<Visit> &visitCatalog)
    {
        std::map<std::pair<int, int>, std::size_t>
            visitIndexByIdentity;

        for (std::size_t visitIndex = 0;
             visitIndex < visitCatalog.size();
             ++visitIndex)
        {
            const Visit &visit = visitCatalog[visitIndex];
            const auto [it, inserted] =
                visitIndexByIdentity.emplace(
                    std::make_pair(
                        visit.nodeIndex,
                        visit.chunkIdx),
                    visitIndex);

            if (!inserted)
            {
                throw std::invalid_argument(
                    "HGS visit catalog contains duplicate "
                    "(nodeIndex, chunkIdx)");
            }
        }

        std::vector<HgsGene> giantTour;
        giantTour.reserve(visitCatalog.size());

        for (const Route &route : solution.routes)
        {
            for (const Visit &stop : route.stops)
            {
                const auto it = visitIndexByIdentity.find(
                    std::make_pair(
                        stop.nodeIndex,
                        stop.chunkIdx));

                if (it == visitIndexByIdentity.end())
                {
                    throw std::invalid_argument(
                        "solution contains a visit that is "
                        "not in the HGS visit catalog");
                }

                giantTour.push_back({it->second});
            }
        }

        if (!isCompleteGiantTourPermutation(
                giantTour, visitCatalog.size()))
        {
            throw std::invalid_argument(
                "solution must contain every HGS visit "
                "exactly once");
        }

        return giantTour;
    }

    HgsRouteSegmentEvaluation evaluateGiantTourSegment(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        std::size_t beginIndex,
        std::size_t endIndex)
    {
        return evaluateGiantTourSegmentImpl(
            inst,
            visitCatalog,
            giantTour,
            beginIndex,
            endIndex,
            nullptr);
    }

    HgsRouteSegmentEvaluation evaluateGiantTourSegment(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        std::size_t beginIndex,
        std::size_t endIndex,
        const std::vector<int> &zoneOf)
    {
        return evaluateGiantTourSegmentImpl(
            inst,
            visitCatalog,
            giantTour,
            beginIndex,
            endIndex,
            &zoneOf);
    }

    HgsIndividualEvaluation aggregateHgsIndividualEvaluation(
        const std::vector<HgsRouteSegmentEvaluation>
            &routeEvaluations,
        const HgsPenaltyWeights &penaltyWeights,
        double zonePenalty)
    {
        validatePenaltyWeights(penaltyWeights, zonePenalty);

        HgsIndividualEvaluation evaluation;

        for (const HgsRouteSegmentEvaluation
                 &routeEvaluation : routeEvaluations)
        {
            if (routeEvaluation.hasCustomerConflict)
            {
                throw std::invalid_argument(
                    "cannot aggregate an HGS route with "
                    "two chunks of the same customer");
            }

            evaluation.distanceCost +=
                routeEvaluation.distanceCost;
            evaluation.routeZoneExcess +=
                routeEvaluation.routeZoneExcess;
            evaluation.violations.weightExcess +=
                routeEvaluation.violations.weightExcess;
            evaluation.violations.volumeExcess +=
                routeEvaluation.violations.volumeExcess;
            evaluation.violations.timeWarp +=
                routeEvaluation.violations.timeWarp;
        }

        evaluation.routeZoneCost =
            zonePenalty *
            static_cast<double>(
                evaluation.routeZoneExcess);
        evaluation.objectiveCost =
            evaluation.distanceCost +
            evaluation.routeZoneCost;
        evaluation.penalizedCost =
            computePenalizedCost(
                evaluation.distanceCost,
                evaluation.routeZoneExcess,
                evaluation.violations,
                penaltyWeights,
                zonePenalty);

        evaluation.feasible =
            evaluation.violations.weightExcess <=
                kWeightTolerance &&
            evaluation.violations.volumeExcess <=
                kVolumeTolerance &&
            evaluation.violations.timeWarp <=
                kTimeTolerance;

        return evaluation;
    }

    HgsIndividual decodeGiantTour(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        const HgsPenaltyWeights &penaltyWeights)
    {
        return decodeGiantTourImpl(
            inst,
            visitCatalog,
            giantTour,
            penaltyWeights,
            nullptr,
            0.0);
    }

    HgsIndividual decodeGiantTour(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty)
    {
        return decodeGiantTourImpl(
            inst,
            visitCatalog,
            giantTour,
            penaltyWeights,
            &zoneOf,
            zonePenalty);
    }

    bool isCompleteGiantTourPermutation(
        const std::vector<HgsGene> &giantTour,
        std::size_t visitCount)
    {
        if (giantTour.size() != visitCount)
        {
            return false;
        }

        std::vector<bool> seen(visitCount, false);

        for (const HgsGene gene : giantTour)
        {
            if (gene.visitIndex >= visitCount ||
                seen[gene.visitIndex])
            {
                return false;
            }

            seen[gene.visitIndex] = true;
        }

        return true;
    }
}
