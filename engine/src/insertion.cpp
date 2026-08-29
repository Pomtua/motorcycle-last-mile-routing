#include "router/insertion.hpp"

#include <vector>

namespace router
{
    namespace
    {
        constexpr double kTimeTolerance = 1e-6;
        constexpr double kMu = 1.0;

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

    }

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

            if (!best.feasible || cost < best.cost)
            {
                best.cost = cost;
                best.position = pos;
                best.feasible = true;
            }
        }

        return best;
    }

    double removalGain(const Instance &inst, const Route &route, std::size_t position)
    {
        const int removedNode = route.stops[position].nodeIndex;
        const int predNode = (position == 0) ? 0 : route.stops[position - 1].nodeIndex;
        const int succNode = (position + 1 == route.stops.size())
                                  ? 0
                                  : route.stops[position + 1].nodeIndex;

        const double dPredRemoved =
            inst.distanceMatrix[static_cast<std::size_t>(predNode)]
                               [static_cast<std::size_t>(removedNode)];
        const double dRemovedSucc =
            inst.distanceMatrix[static_cast<std::size_t>(removedNode)]
                               [static_cast<std::size_t>(succNode)];
        const double dPredSucc =
            inst.distanceMatrix[static_cast<std::size_t>(predNode)]
                               [static_cast<std::size_t>(succNode)];

        return dPredRemoved + dRemovedSucc - kMu * dPredSucc;
    }

}
