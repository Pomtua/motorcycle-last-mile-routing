#pragma once

#include <vector>

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    struct ZoneMetrics
    {
        int fragmentation = 0;
        double averageZonesPerRoute = 0.0;
    };

    std::vector<int> assignZones(const Instance &inst, int numZones);
    ZoneMetrics measureZoneCoherence(
        const Solution &solution,
        const std::vector<int> &zoneOf);
}
