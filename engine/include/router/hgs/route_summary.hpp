#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>

#include "router/hgs/delivery.hpp"
#include "router/hgs/problem_data.hpp"

namespace router::hgs
{
    struct RouteSummary
    {
        int first = ProblemData::kDepot;
        int last = ProblemData::kDepot;
        int visits = 0;
        double distance = 0.0;
        double duration = 0.0;
        double timeWarp = 0.0;
        double earliest = 0.0;
        double latest = 0.0;
        double weight = 0.0;
        double volume = 0.0;
        std::uint64_t zones = 0;
    };

    inline RouteSummary depotSummary(const ProblemData &data)
    {
        RouteSummary summary;
        summary.latest = data.horizon();
        return summary;
    }

    inline RouteSummary visitSummary(const ProblemData &data, const Delivery &delivery)
    {
        const CustomerData &customer = data.customer(delivery.customer);
        RouteSummary summary;
        summary.first = delivery.customer;
        summary.last = delivery.customer;
        summary.visits = 1;
        summary.duration = customer.serviceTime;
        summary.earliest = customer.twStart;
        summary.latest = customer.twEnd;
        summary.weight = toWeight(delivery.units);
        summary.volume = data.volumeOf(delivery);
        summary.zones = customer.zoneMask;
        return summary;
    }

    inline RouteSummary concat(const ProblemData &data, const RouteSummary &a, const RouteSummary &b)
    {
        const double travel = data.duration(a.last, b.first);
        const double delta = a.duration - a.timeWarp + travel;
        const double wait = std::max(b.earliest - delta - a.latest, 0.0);
        const double warp = std::max(a.earliest + delta - b.latest, 0.0);

        RouteSummary summary;
        summary.first = a.first;
        summary.last = b.last;
        summary.visits = a.visits + b.visits;
        summary.distance = a.distance + b.distance + data.distance(a.last, b.first);
        summary.duration = a.duration + b.duration + travel + wait;
        summary.timeWarp = a.timeWarp + b.timeWarp + warp;
        summary.earliest = std::max(b.earliest - delta, a.earliest) - wait;
        summary.latest = std::min(b.latest - delta, a.latest) + warp;
        summary.weight = a.weight + b.weight;
        summary.volume = a.volume + b.volume;
        summary.zones = a.zones | b.zones;
        return summary;
    }

    template <typename DeliveryRange>
    RouteSummary summarizeRoute(const ProblemData &data, const DeliveryRange &deliveries)
    {
        RouteSummary summary = depotSummary(data);
        for (const Delivery &delivery : deliveries)
        {
            summary = concat(data, summary, visitSummary(data, delivery));
        }
        return concat(data, summary, depotSummary(data));
    }

    inline int zoneExcess(const RouteSummary &summary)
    {
        return summary.zones == 0 ? 0 : std::popcount(summary.zones) - 1;
    }
}
