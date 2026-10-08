#include "router/hgs/split.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include "router/hgs/route_summary.hpp"

namespace router::hgs
{
    namespace
    {
        struct Arc
        {
            std::size_t end = 0;
            double cost = 0.0;
        };

        std::vector<std::vector<Arc>> buildArcs(
            const ProblemData &data,
            const std::vector<int> &giantTour,
            const Penalties &penalties)
        {
            const double weightLimit = kSplitLoadLimitFactor * data.weightCapacity() + kWeightTolerance;
            const double volumeLimit = kSplitLoadLimitFactor * data.volumeCapacity() + kVolumeTolerance;
            std::vector<std::size_t> segmentOfVisit(data.visitCount() + 1, giantTour.size());
            std::vector<std::vector<Arc>> arcs(giantTour.size());
            const auto conflictsWithSegment = [&](int visit, std::size_t begin)
            {
                const auto &siblings = data.siblings(visit);
                return std::any_of(siblings.begin(), siblings.end(), [&](int sibling)
                                   { return segmentOfVisit[static_cast<std::size_t>(sibling)] == begin; });
            };

            for (std::size_t begin = 0; begin < giantTour.size(); ++begin)
            {
                RouteSummary open = depotSummary(data);
                for (std::size_t end = begin; end < giantTour.size(); ++end)
                {
                    const int visit = giantTour[end];
                    if (conflictsWithSegment(visit, begin))
                    {
                        break;
                    }
                    segmentOfVisit[static_cast<std::size_t>(visit)] = begin;
                    open = concat(data, open, visitSummary(data, visit));
                    if (open.weight > weightLimit || open.volume > volumeLimit)
                    {
                        break;
                    }
                    const RouteSummary closed = concat(data, open, depotSummary(data));
                    arcs[begin].push_back({end + 1, penalizedCost(data, closed, penalties)});
                }
            }
            return arcs;
        }
    }

    std::optional<Individual> split(
        const ProblemData &data,
        const std::vector<int> &giantTour,
        const Penalties &penalties)
    {
        validatePenalties(penalties);
        if (!isCompletePermutation(giantTour, data.visitCount()))
        {
            throw std::invalid_argument("Split requires a complete giant-tour permutation");
        }
        if (giantTour.empty())
        {
            return makeIndividual(data, {});
        }

        const std::size_t visitCount = giantTour.size();
        const std::size_t maxRoutes = std::min(visitCount, static_cast<std::size_t>(data.fleetSize()));
        if (maxRoutes == 0)
        {
            return std::nullopt;
        }

        const auto arcs = buildArcs(data, giantTour, penalties);
        const double infinity = std::numeric_limits<double>::infinity();
        const std::size_t noParent = visitCount + 1;
        std::vector<std::vector<std::size_t>> parent(maxRoutes + 1, std::vector<std::size_t>(visitCount + 1, noParent));
        std::vector<double> previous(visitCount + 1, infinity);
        std::vector<double> current(visitCount + 1, infinity);
        previous[0] = 0.0;

        double bestCost = infinity;
        std::size_t bestRouteCount = 0;
        for (std::size_t routeCount = 1; routeCount <= maxRoutes; ++routeCount)
        {
            std::fill(current.begin(), current.end(), infinity);
            for (std::size_t begin = 0; begin < visitCount; ++begin)
            {
                if (previous[begin] == infinity)
                {
                    continue;
                }
                for (const Arc &arc : arcs[begin])
                {
                    const double candidate = previous[begin] + arc.cost;
                    if (candidate < current[arc.end])
                    {
                        current[arc.end] = candidate;
                        parent[routeCount][arc.end] = begin;
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
            return std::nullopt;
        }

        std::vector<std::vector<int>> routes(bestRouteCount);
        std::size_t end = visitCount;
        for (std::size_t routeCount = bestRouteCount; routeCount > 0; --routeCount)
        {
            const std::size_t begin = parent[routeCount][end];
            routes[routeCount - 1].assign(
                giantTour.begin() + static_cast<std::ptrdiff_t>(begin),
                giantTour.begin() + static_cast<std::ptrdiff_t>(end));
            end = begin;
        }
        return makeIndividual(data, std::move(routes));
    }
}
