#include "router/cost.hpp"

namespace router
{
    double computeCost(const Instance &inst, const Solution &solution)
    {
        double total = 0.0;
        for (const Route &route : solution.routes)
        {
            int currentNode = 0;

            for (const Visit &stop : route.stops)
            {
                total += inst.distanceMatrix[static_cast<std::size_t>(currentNode)]
                                            [static_cast<std::size_t>(stop.nodeIndex)];
                currentNode = stop.nodeIndex;
            }

            total += inst.distanceMatrix[static_cast<std::size_t>(currentNode)][0];
        }
        return total;
    }
}