#pragma once

#include "router/hgs/problem_data.hpp"
#include "router/hgs/route_summary.hpp"

namespace router::hgs
{
    inline constexpr double kWeightTolerance = 1e-6;
    inline constexpr double kVolumeTolerance = 1e-9;
    inline constexpr double kTimeTolerance = 1e-6;

    struct Penalties
    {
        double weight = 0.0;
        double volume = 0.0;
        double timeWarp = 0.0;
    };

    struct CostBreakdown
    {
        double distance = 0.0;
        int zoneExcess = 0;
        double weightExcess = 0.0;
        double volumeExcess = 0.0;
        double timeWarp = 0.0;
        int routes = 0;

        CostBreakdown &operator+=(const CostBreakdown &other)
        {
            distance += other.distance;
            zoneExcess += other.zoneExcess;
            weightExcess += other.weightExcess;
            volumeExcess += other.volumeExcess;
            timeWarp += other.timeWarp;
            routes += other.routes;
            return *this;
        }
    };

    void validatePenalties(const Penalties &penalties);

    inline double excessAbove(double value, double limit, double tolerance)
    {
        return value <= limit + tolerance ? 0.0 : value - limit;
    }

    inline CostBreakdown routeCost(const ProblemData &data, const RouteSummary &route)
    {
        if (route.visits == 0)
        {
            return {};
        }
        CostBreakdown cost;
        cost.distance = route.distance;
        cost.zoneExcess = zoneExcess(route);
        cost.weightExcess = excessAbove(route.weight, data.weightCapacity(), kWeightTolerance);
        cost.volumeExcess = excessAbove(route.volume, data.volumeCapacity(), kVolumeTolerance);
        cost.timeWarp = route.timeWarp > kTimeTolerance ? route.timeWarp : 0.0;
        cost.routes = 1;
        return cost;
    }

    inline bool isFeasible(const CostBreakdown &cost)
    {
        return cost.weightExcess <= kWeightTolerance &&
               cost.volumeExcess <= kVolumeTolerance &&
               cost.timeWarp <= kTimeTolerance;
    }

    inline double objectiveCost(const ProblemData &data, const CostBreakdown &cost)
    {
        return cost.distance + data.zonePenalty() * static_cast<double>(cost.zoneExcess);
    }

    inline double penalizedCost(const ProblemData &data, const CostBreakdown &cost, const Penalties &penalties)
    {
        return objectiveCost(data, cost) +
               penalties.weight * cost.weightExcess +
               penalties.volume * cost.volumeExcess +
               penalties.timeWarp * cost.timeWarp;
    }

    inline double penalizedCost(const ProblemData &data, const RouteSummary &route, const Penalties &penalties)
    {
        return penalizedCost(data, routeCost(data, route), penalties);
    }
}
