#include "router/solomon_i1.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "router/split.hpp"

namespace router
{
    namespace
    {
        constexpr double kTimeTolerance = 1e-6;
        constexpr double kMu = 1.0;
        constexpr double kLambda = 2.0;

        bool hasCustomer(const Route &route, int nodeIndex)
        {
            for (const Visit &s : route.stops)
            {
                if (s.nodeIndex == nodeIndex)
                {
                    return true;
                }
            }
            return false;
        }

        bool fitsCapacity(const Instance &inst, const Route &route, const Visit &visit)
        {
            double w = visit.weight, v = visit.volume;
            for (const Visit &s : route.stops)
            {
                w += s.weight;
                v += s.volume;
            }
            return w <= inst.fleet.weightCapacity + 1e-6 &&
                   v <= inst.fleet.volumeCapacity + 1e-9;
        }

        std::vector<double> forwardDepartures(const Instance &inst, const Route &route)
        {
            std::vector<double> departures(route.stops.size());
            double clock = 0.0;
            int currentNode = 0;

            for (std::size_t k = 0; k < route.stops.size(); ++k)
            {
                const Visit &s = route.stops[k];
                const Node &node = inst.nodes[static_cast<std::size_t>(s.nodeIndex)];

                double arrival = clock + inst.durationMatrix[static_cast<std::size_t>(currentNode)]
                                                            [static_cast<std::size_t>(s.nodeIndex)];
                if (arrival < node.twStart)
                {
                    arrival = node.twStart;
                }
                clock = arrival + node.serviceTime;
                departures[k] = clock;
                currentNode = s.nodeIndex;
            }
            return departures;
        }

        bool tailFeasible(const Instance &inst, const Route &route, std::size_t from,
                          int fromNode, double clock)
        {
            int currentNode = fromNode;
            double t = clock;

            for (std::size_t k = from; k < route.stops.size(); ++k)
            {
                const Visit &s = route.stops[k];
                const Node &node = inst.nodes[static_cast<std::size_t>(s.nodeIndex)];

                double arrival = t + inst.durationMatrix[static_cast<std::size_t>(currentNode)]
                                                        [static_cast<std::size_t>(s.nodeIndex)];
                if (arrival < node.twStart)
                {
                    arrival = node.twStart;
                }
                if (arrival > node.twEnd + kTimeTolerance)
                {
                    return false;
                }
                t = arrival + node.serviceTime;
                currentNode = s.nodeIndex;
            }

            const double returnTravel =
                inst.durationMatrix[static_cast<std::size_t>(currentNode)][0];
            return t + returnTravel <= inst.horizon + kTimeTolerance;
        }

        struct InsertionCandidate
        {
            std::size_t position = 0;
            double cost = std::numeric_limits<double>::max();
            bool feasible = false;
        };

        InsertionCandidate bestInsertion(const Instance &inst, const Route &route,
                                         const Visit &visit)
        {
            InsertionCandidate best;

            if (hasCustomer(route, visit.nodeIndex) || !fitsCapacity(inst, route, visit))
            {
                return best;
            }

            const std::vector<double> departures = forwardDepartures(inst, route);
            const Node &uNode = inst.nodes[static_cast<std::size_t>(visit.nodeIndex)];

            for (std::size_t pos = 0; pos <= route.stops.size(); ++pos)
            {
                const int predNode = (pos == 0) ? 0 : route.stops[pos - 1].nodeIndex;
                const int succNode =
                    (pos == route.stops.size()) ? 0 : route.stops[pos].nodeIndex;
                const double predDeparture = (pos == 0) ? 0.0 : departures[pos - 1];

                double arrival = predDeparture +
                                 inst.durationMatrix[static_cast<std::size_t>(predNode)]
                                                    [static_cast<std::size_t>(visit.nodeIndex)];
                if (arrival < uNode.twStart)
                {
                    arrival = uNode.twStart;
                }
                if (arrival > uNode.twEnd + kTimeTolerance)
                {
                    continue;
                }
                const double departureAtU = arrival + uNode.serviceTime;

                if (!tailFeasible(inst, route, pos, visit.nodeIndex, departureAtU))
                {
                    continue;
                }

                const double dPredU =
                    inst.distanceMatrix[static_cast<std::size_t>(predNode)]
                                       [static_cast<std::size_t>(visit.nodeIndex)];
                const double dUSucc =
                    inst.distanceMatrix[static_cast<std::size_t>(visit.nodeIndex)]
                                       [static_cast<std::size_t>(succNode)];
                const double dPredSucc =
                    inst.distanceMatrix[static_cast<std::size_t>(predNode)]
                                       [static_cast<std::size_t>(succNode)];

                const double cost = dPredU + dUSucc - kMu * dPredSucc;

                if (cost < best.cost)
                {
                    best.cost = cost;
                    best.position = pos;
                    best.feasible = true;
                }
            }

            return best;
        }

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

    Solution solomonI1(const Instance &inst)
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

            if (solution.routes.size() >= static_cast<std::size_t>(inst.fleet.size))
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