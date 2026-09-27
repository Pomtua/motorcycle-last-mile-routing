#pragma once

#include <vector>

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    struct ZoneSelection
    {
        int numZones = 0;
        int candidateMaxZones = 0;
        double silhouetteScore = 0.0;
        std::vector<int> zoneOf;
    };

    struct ZoneMetrics
    {
        int fragmentation = 0;
        int routeZoneExcess = 0;
        double averageZonesPerRoute = 0.0;
    };

    std::vector<int> assignZones(const Instance &inst, int numZones);
    ZoneSelection selectZones(const Instance &inst);
    ZoneSelection selectZones(
        const Instance &inst,
        int candidateMaxZones);
    ZoneMetrics measureZoneCoherence(
        const Solution &solution,
        const std::vector<int> &zoneOf);
    double computeRouteZoneCost(
        const Solution &solution,
        const std::vector<int> &zoneOf,
        double penaltyPerExtraZone);
}
