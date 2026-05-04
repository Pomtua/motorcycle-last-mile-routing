#pragma once
#include <vector>
#include <string>

namespace routing {

struct Route {
    int vehicle_id;
    std::vector<int> parcel_ids;     
    std::vector<int> location_ids;   
    std::vector<double> arrival_times;
    double route_distance = 0.0;
    double route_duration = 0.0;
    double total_weight = 0.0;
    double total_volume = 0.0;
};

struct RoutingResult {
    std::string solver_name;
    std::vector<Route> routes;
    double total_distance = 0.0;
    double total_duration = 0.0;
    int undelivered_count = 0;
    double total_cost = 0.0;
    bool all_parcels_delivered = false;
    double execution_time_ms = 0.0;
};

}
