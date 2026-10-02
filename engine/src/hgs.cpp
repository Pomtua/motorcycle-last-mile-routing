#include "router/hgs.hpp"

#include <algorithm>
#include <cmath>
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
