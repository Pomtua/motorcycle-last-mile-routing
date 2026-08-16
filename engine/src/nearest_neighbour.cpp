#include "router/nearest_neighbour.hpp"

#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "router/split.hpp"

namespace router
{
    namespace
    {
        constexpr double kTimeTolerance = 1e-6;

        struct VehicleState
        {
            int currentNode = 0;
            double clock = 0.0;
            double weight = 0.0;
            double volume = 0.0;
            std::set<int> customersServed;
        };

        bool canAppend(const Instance &inst, const VehicleState &state,
                       const Visit &visit, double &arrivalOut)
        {
            if (state.customersServed.count(visit.nodeIndex) > 0)
            {
                return false;
            }

            if (state.weight + visit.weight > inst.fleet.weightCapacity + 1e-6)
            {
                return false;
            }
            if (state.volume + visit.volume > inst.fleet.volumeCapacity + 1e-9)
            {
                return false;
            }

            const Node &node = inst.nodes[static_cast<std::size_t>(visit.nodeIndex)];

            const double travel = inst.durationMatrix[static_cast<std::size_t>(state.currentNode)]
                                                     [static_cast<std::size_t>(visit.nodeIndex)];
            double arrival = state.clock + travel;
            if (arrival < node.twStart)
            {
                arrival = node.twStart;
            }
            if (arrival > node.twEnd + kTimeTolerance)
            {
                return false;
            }

            const double departure = arrival + node.serviceTime;
            const double returnTravel =
                inst.durationMatrix[static_cast<std::size_t>(visit.nodeIndex)][0];
            if (departure + returnTravel > inst.horizon + kTimeTolerance)
            {
                return false;
            }

            arrivalOut = arrival;
            return true;
        }

    }

    Solution nearestNeighbour(const Instance &inst)
    {
        const std::vector<Visit> allVisits = splitCustomers(inst);

        std::vector<bool> unserved(allVisits.size(), true);
        std::size_t remaining = allVisits.size();

        Solution solution;

        while (remaining > 0)
        {
            VehicleState state;
            Route route;

            while (true)
            {
                std::size_t bestIdx = allVisits.size();
                double bestDistance = std::numeric_limits<double>::max();
                double bestArrival = 0.0;

                for (std::size_t i = 0; i < allVisits.size(); ++i)
                {
                    if (!unserved[i])
                    {
                        continue;
                    }

                    double arrival = 0.0;
                    if (!canAppend(inst, state, allVisits[i], arrival))
                    {
                        continue;
                    }

                    const double distance =
                        inst.distanceMatrix[static_cast<std::size_t>(state.currentNode)]
                                           [static_cast<std::size_t>(allVisits[i].nodeIndex)];

                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        bestIdx = i;
                        bestArrival = arrival;
                    }
                }

                if (bestIdx == allVisits.size())
                {
                    break;
                }

                const Visit &chosen = allVisits[bestIdx];
                const Node &node = inst.nodes[static_cast<std::size_t>(chosen.nodeIndex)];

                route.stops.push_back(chosen);
                state.currentNode = chosen.nodeIndex;
                state.clock = bestArrival + node.serviceTime;
                state.weight += chosen.weight;
                state.volume += chosen.volume;
                state.customersServed.insert(chosen.nodeIndex);

                unserved[bestIdx] = false;
                --remaining;
            }

            if (route.stops.empty())
            {
                throw std::runtime_error(
                    "nearestNeighbour: " + std::to_string(remaining) +
                    " chunk(s) cannot be served by any vehicle — instance is "
                    "infeasible for this heuristic");
            }

            if (solution.routes.size() >= static_cast<std::size_t>(inst.fleet.size))
            {
                throw std::runtime_error(
                    "nearestNeighbour: fleet exhausted (" +
                    std::to_string(inst.fleet.size) + " vehicles) with " +
                    std::to_string(remaining) + " chunk(s) unserved");
            }

            solution.routes.push_back(std::move(route));
        }

        return solution;
    }

}