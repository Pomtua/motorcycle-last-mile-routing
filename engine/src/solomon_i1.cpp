#include "router/solomon_i1.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "router/insertion.hpp"
#include "router/split.hpp"

namespace router
{
    namespace
    {
        constexpr double kLambda = 2.0;

        std::size_t chooseSeed(const Instance &inst, const std::vector<Visit> &allVisits,
                               const std::vector<bool> &unserved)
        {
            std::size_t bestIdx = allVisits.size();
            double bestDistance = -1.0;
            const Route empty;

            for (std::size_t i = 0; i < allVisits.size(); ++i)
            {
                if (!unserved[i])
                {
                    continue;
                }

                const double d = inst.distanceMatrix[0]
                                                    [static_cast<std::size_t>(allVisits[i].nodeIndex)];

                if (d > bestDistance && bestInsertion(inst, empty, allVisits[i]).feasible)
                {
                    bestDistance = d;
                    bestIdx = i;
                }
            }
            return bestIdx;
        }

        Route buildRoute(const Instance &inst, const std::vector<Visit> &allVisits,
                         std::vector<bool> &unserved, std::size_t &remaining,
                         std::size_t seedIdx)
        {
            Route route;
            route.stops.push_back(allVisits[seedIdx]);
            unserved[seedIdx] = false;
            --remaining;

            while (true)
            {
                std::size_t bestVisitIdx = allVisits.size();
                InsertionCandidate bestCandidate;
                double bestC2 = -std::numeric_limits<double>::max();

                for (std::size_t i = 0; i < allVisits.size(); ++i)
                {
                    if (!unserved[i])
                    {
                        continue;
                    }

                    const InsertionCandidate cand = bestInsertion(inst, route, allVisits[i]);
                    if (!cand.feasible)
                    {
                        continue;
                    }

                    const double d0u =
                        inst.distanceMatrix[0][static_cast<std::size_t>(allVisits[i].nodeIndex)];
                    const double c2 = kLambda * d0u - cand.cost;

                    if (c2 > bestC2)
                    {
                        bestC2 = c2;
                        bestVisitIdx = i;
                        bestCandidate = cand;
                    }
                }

                if (bestVisitIdx == allVisits.size())
                {
                    break;
                }

                route.stops.insert(route.stops.begin() +
                                       static_cast<std::ptrdiff_t>(bestCandidate.position),
                                   allVisits[bestVisitIdx]);
                unserved[bestVisitIdx] = false;
                --remaining;
            }

            return route;
        }

    }

    Solution solomonI1(const Instance &inst, bool enforceFleet)
    {
        const std::vector<Visit> allVisits = splitCustomers(inst);

        std::vector<bool> unserved(allVisits.size(), true);
        std::size_t remaining = allVisits.size();

        Solution solution;

        while (remaining > 0)
        {
            const std::size_t seedIdx = chooseSeed(inst, allVisits, unserved);

            if (seedIdx == allVisits.size())
            {
                throw std::runtime_error(
                    "solomonI1: " + std::to_string(remaining) +
                    " chunk(s) cannot be served by any vehicle — instance is "
                    "infeasible for this heuristic");
            }

            Route route = buildRoute(inst, allVisits, unserved, remaining, seedIdx);

            if (enforceFleet && solution.routes.size() >= static_cast<std::size_t>(inst.fleet.size))
            {
                throw std::runtime_error(
                    "solomonI1: fleet exhausted -- needs at least " +
                    std::to_string(solution.routes.size() + 1) + " vehicles, fleet has " +
                    std::to_string(inst.fleet.size) + " (" +
                    std::to_string(remaining) + " chunk(s) still unserved)");
            }

            solution.routes.push_back(std::move(route));
        }

        return solution;
    }

}