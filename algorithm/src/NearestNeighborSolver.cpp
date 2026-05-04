#include "NearestNeighborSolver.h"
#include <algorithm>
#include <limits>
#include <chrono>
#include <iostream>

namespace routing {

RoutingResult NearestNeighborSolver::solve(const RoutingInstance& instance) {
    auto start_time = std::chrono::high_resolution_clock::now();
    RoutingResult result = createResult("FeasibleNearestNeighbor");

    std::set<int> unassigned_parcels;
    for (int i = 0; i < instance.parcels.size(); ++i) {
        unassigned_parcels.insert(i);
    }

    for (const auto& vehicle : instance.vehicles) {
        if (unassigned_parcels.empty()) break;

        Route route;
        route.vehicle_id = vehicle.id;
        route.location_ids.push_back(0); 
        route.arrival_times.push_back(0.0);

        double current_weight = 0.0;
        double current_volume = 0.0;
        double current_time = 0.0;
        int current_loc_idx = 0;

        while (!unassigned_parcels.empty()) {
            int best_parcel_idx = -1;
            double min_dist = std::numeric_limits<double>::max();
            double best_arrival_time = 0.0;

            for (int p_idx : unassigned_parcels) {
                int next_loc_idx = instance.id_to_index.at(instance.parcels[p_idx].location_id);
                double dist = instance.distance_matrix[current_loc_idx][next_loc_idx];

                double arrival_time = 0.0;
                if (dist < min_dist && isFeasible(instance, route, p_idx, current_weight, current_volume, current_time, arrival_time)) {
                    min_dist = dist;
                    best_parcel_idx = p_idx;
                    best_arrival_time = arrival_time;
                }
            }

            if (best_parcel_idx != -1) {

                int next_loc_idx = instance.id_to_index.at(instance.parcels[best_parcel_idx].location_id);

                route.parcel_ids.push_back(instance.parcels[best_parcel_idx].id);
                route.location_ids.push_back(instance.parcels[best_parcel_idx].location_id);
                route.arrival_times.push_back(best_arrival_time);
                route.route_distance += min_dist;
                route.route_duration += instance.duration_matrix[current_loc_idx][next_loc_idx];

                current_weight += instance.parcels[best_parcel_idx].weight;
                current_volume += instance.parcels[best_parcel_idx].volume;
                current_time = best_arrival_time; 
                current_loc_idx = next_loc_idx;

                unassigned_parcels.erase(best_parcel_idx);
            } else {

                break;
            }
        }

        double dist_to_depot = instance.distance_matrix[current_loc_idx][0];
        route.route_distance += dist_to_depot;
        route.route_duration += instance.duration_matrix[current_loc_idx][0];
        route.location_ids.push_back(0);

        result.routes.push_back(route);
        result.total_distance += route.route_distance;
        result.total_duration += route.route_duration;
    }

    result.all_parcels_delivered = unassigned_parcels.empty();
    result.undelivered_count = unassigned_parcels.size();
    result.total_cost = result.total_distance + (result.undelivered_count * Solver::PENALTY_PER_UNDELIVERED);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return result;
}

bool NearestNeighborSolver::isFeasible(const RoutingInstance& instance, 
                                       const Route& current_route, 
                                       int p_idx,
                                       double current_weight,
                                       double current_volume,
                                       double current_time,
                                       double& arrival_time) {
    const auto& parcel = instance.parcels[p_idx];
    const auto& vehicle = instance.vehicles[current_route.vehicle_id];

    if (current_weight + parcel.weight > vehicle.capacity_weight) return false;
    if (current_volume + parcel.volume > vehicle.capacity_volume) return false;

    int current_loc_idx = instance.id_to_index.at(current_route.location_ids.back());
    int next_loc_idx = instance.id_to_index.at(parcel.location_id);
    double travel_time_minutes = instance.duration_matrix[current_loc_idx][next_loc_idx] / 60.0;

    arrival_time = current_time + travel_time_minutes;

    if (arrival_time < parcel.time_window.start) {
        arrival_time = parcel.time_window.start;
    }

    if (arrival_time > parcel.time_window.end) return false;

    return true;
}

}
