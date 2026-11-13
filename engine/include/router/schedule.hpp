#pragma once

#include <vector>

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    struct StopTiming
    {
        double arrival = 0.0;
        double start = 0.0;
        double departure = 0.0;
        bool onTime = true;
    };

    struct RouteSchedule
    {
        std::vector<StopTiming> stops;
        double distance = 0.0;
        double returnTime = 0.0;
        double loadWeight = 0.0;
        double loadVolume = 0.0;
        bool withinHorizon = true;
    };

    RouteSchedule scheduleRoute(const Instance &inst, const Route &route);
}
