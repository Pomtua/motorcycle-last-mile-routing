#include "ConstraintChecker.h"
#include <algorithm>

namespace routing {

bool ConstraintChecker::evaluateRoute(const RoutingInstance& instance, Route& route, const Vehicle& vehicle) {
    if (route.location_ids.empty()) return true;

    double current_weight = 0.0;
    double current_volume = 0.0;
    for (int pid : route.parcel_ids) {
        const auto& p = instance.parcels[instance.parcel_id_to_index[pid]];
        current_weight += p.weight;
        current_volume += p.volume;
    }

    if (current_weight > vehicle.capacity_weight || current_volume > vehicle.capacity_volume) {
        return false;
    }

    route.total_weight = current_weight;
    route.total_volume = current_volume;

    double distance = 0.0;
    double duration = 0.0;
    double current_time = 0.0;
    std::vector<double> arrival_times(route.location_ids.size(), 0.0);

    int current_loc = route.location_ids[0]; 

    for (size_t i = 1; i < route.location_ids.size(); ++i) {
        int next_loc = route.location_ids[i];

        int idx1 = instance.id_to_index[current_loc];
        int idx2 = instance.id_to_index[next_loc];

        distance += instance.distance_matrix[idx1][idx2];
        duration += instance.duration_matrix[idx1][idx2];

        double travel_time_min = instance.duration_matrix[idx1][idx2] / 60.0;
        current_time += travel_time_min;

        if (i < route.location_ids.size() - 1) {

            int pid = route.parcel_ids[i - 1];
            const auto& p = instance.parcels[instance.parcel_id_to_index[pid]];

            if (current_time < p.time_window.start) {
                current_time = p.time_window.start;
            }
            if (current_time > p.time_window.end) {
                return false; 
            }
        }

        arrival_times[i] = current_time;
        current_loc = next_loc;
    }

    route.route_distance = distance;
    route.route_duration = duration;
    route.arrival_times = arrival_times;

    return true;
}

bool ConstraintChecker::checkInsertionFeasibility(const RoutingInstance& instance, 
                                                  const Route& route, 
                                                  const Vehicle& vehicle, 
                                                  const Parcel& parcel, 
                                                  int pos, 
                                                  double& cost_increase) {

    if (route.total_weight + parcel.weight > vehicle.capacity_weight || 
        route.total_volume + parcel.volume > vehicle.capacity_volume) return false;

    int prev_loc = route.location_ids[pos - 1];
    int next_loc = route.location_ids[pos];

    int prev_idx = instance.id_to_index[prev_loc];
    int next_idx = instance.id_to_index[next_loc];
    int curr_idx = instance.id_to_index[parcel.location_id];

    double d1 = instance.distance_matrix[prev_idx][curr_idx];
    double d2 = instance.distance_matrix[curr_idx][next_idx];
    double d_old = instance.distance_matrix[prev_idx][next_idx];

    cost_increase = d1 + d2 - d_old;

    double current_time = route.arrival_times[pos - 1];

    current_time += (instance.duration_matrix[prev_idx][curr_idx] / 60.0);
    if (current_time < parcel.time_window.start) current_time = parcel.time_window.start;
    if (current_time > parcel.time_window.end) return false;

    double prev_arr_time = current_time;
    int prev_node_idx = curr_idx;

    for (size_t i = pos; i < route.location_ids.size(); ++i) {
        int next_node_id = route.location_ids[i];
        int next_node_idx = instance.id_to_index[next_node_id];

        double travel_time = instance.duration_matrix[prev_node_idx][next_node_idx] / 60.0;
        double arrival_time = prev_arr_time + travel_time;

        if (i < route.location_ids.size() - 1) {
            int pid = route.parcel_ids[i - 1];
            const auto& p = instance.parcels[instance.parcel_id_to_index[pid]];
            if (arrival_time < p.time_window.start) arrival_time = p.time_window.start;
            if (arrival_time > p.time_window.end) return false;
        }

        if (arrival_time <= route.arrival_times[i]) {
            break; 
        }

        prev_arr_time = arrival_time;
        prev_node_idx = next_node_idx;
    }

    return true;
}

}
