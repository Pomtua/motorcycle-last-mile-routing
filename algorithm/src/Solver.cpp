#include "Solver.h"
#include <set>
#include <algorithm>
#include <limits>

namespace routing {

void Solver::repairUndelivered(const RoutingInstance& instance, RoutingResult& result) {
    if (result.undelivered_count == 0) return;

    std::set<int> delivered_ids;
    for (const auto& route : result.routes) {
        for (int pid : route.parcel_ids) {
            delivered_ids.insert(pid);
        }
    }

    std::vector<const Parcel*> undelivered;
    for (const auto& p : instance.parcels) {
        if (!delivered_ids.count(p.id)) {
            undelivered.push_back(&p);
        }
    }

    std::stable_sort(undelivered.begin(), undelivered.end(),
        [](const Parcel* a, const Parcel* b) {
            return a->is_split && !b->is_split;
        });

    for (const auto* parcel_ptr : undelivered) {
        const Parcel& parcel = *parcel_ptr;
        double best_cost = std::numeric_limits<double>::max();
        int best_route = -1;
        int best_pos = -1;

        for (size_t r = 0; r < result.routes.size(); r++) {
            const Route& route = result.routes[r];
            if (route.vehicle_id >= (int)instance.vehicles.size()) continue;
            const Vehicle& vehicle = instance.vehicles[route.vehicle_id];

            for (size_t pos = 1; pos < route.location_ids.size(); pos++) {
                double cost_inc = 0.0;
                if (ConstraintChecker::checkInsertionFeasibility(
                        instance, route, vehicle, parcel, pos, cost_inc)) {
                    if (cost_inc < best_cost) {
                        best_cost = cost_inc;
                        best_route = (int)r;
                        best_pos = (int)pos;
                    }
                }
            }
        }

        if (best_route >= 0) {
            Route& route = result.routes[best_route];
            route.parcel_ids.insert(
                route.parcel_ids.begin() + (best_pos - 1), parcel.id);
            route.location_ids.insert(
                route.location_ids.begin() + best_pos, parcel.location_id);
            const Vehicle& vehicle = instance.vehicles[route.vehicle_id];
            ConstraintChecker::evaluateRoute(instance, route, vehicle);
            result.undelivered_count--;
        }
    }

    result.all_parcels_delivered = (result.undelivered_count == 0);
    result.total_distance = 0.0;
    result.total_duration = 0.0;
    for (const auto& r : result.routes) {
        result.total_distance += r.route_distance;
        result.total_duration += r.route_duration;
    }
    result.total_cost = result.total_distance +
                        result.undelivered_count * PENALTY_PER_UNDELIVERED;
}

} // namespace routing
