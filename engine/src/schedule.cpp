#include "router/schedule.hpp"

#include <algorithm>

namespace router
{
    namespace
    {
        constexpr double kTimeTolerance = 1e-6;
    }

    RouteSchedule scheduleRoute(const Instance &inst, const Route &route)
    {
        RouteSchedule schedule;
        schedule.stops.reserve(route.stops.size());
        double clock = 0.0;
        std::size_t current = 0;
        for (const Visit &stop : route.stops)
        {
            const auto next = static_cast<std::size_t>(stop.nodeIndex);
            const Node &node = inst.nodes[next];
            StopTiming timing;
            timing.arrival = clock + inst.durationMatrix[current][next];
            timing.start = std::max(timing.arrival, static_cast<double>(node.twStart));
            timing.departure = timing.start + node.serviceTime;
            timing.onTime = timing.start <= node.twEnd + kTimeTolerance;
            schedule.distance += inst.distanceMatrix[current][next];
            schedule.loadWeight += stop.weight;
            schedule.loadVolume += stop.volume;
            schedule.stops.push_back(timing);
            clock = timing.departure;
            current = next;
        }
        schedule.distance += inst.distanceMatrix[current][0];
        schedule.returnTime = clock + inst.durationMatrix[current][0];
        schedule.withinHorizon = schedule.returnTime <= inst.horizon + kTimeTolerance;
        return schedule;
    }
}
