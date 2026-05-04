#include "ALNSOperators.h"
#include "ConstraintChecker.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <limits>

namespace routing {

std::vector<int> ALNSOperators::destroyRandom(const RoutingInstance& instance, RoutingResult& solution, int num_to_remove) {
    std::vector<int> removed;
    std::mt19937 rng(42); 

    std::vector<std::pair<int, int>> all_parcels; 
    for (size_t r = 0; r < solution.routes.size(); ++r) {
        for (int pid : solution.routes[r].parcel_ids) {
            all_parcels.push_back({r, pid});
        }
    }

    std::shuffle(all_parcels.begin(), all_parcels.end(), rng);

    int to_remove = std::min(num_to_remove, (int)all_parcels.size());

    for (int i = 0; i < to_remove; ++i) {
        int r_idx = all_parcels[i].first;
        int pid = all_parcels[i].second;
        removed.push_back(pid);

        auto& route = solution.routes[r_idx];
        auto it = std::find(route.parcel_ids.begin(), route.parcel_ids.end(), pid);
        if (it != route.parcel_ids.end()) {
            int p_idx = std::distance(route.parcel_ids.begin(), it);
            route.parcel_ids.erase(it);
            route.location_ids.erase(route.location_ids.begin() + p_idx + 1); 
        }
    }

    for (auto& route : solution.routes) {
        const auto& vehicle = instance.vehicles[route.vehicle_id];
        ConstraintChecker::evaluateRoute(instance, route, vehicle);
    }

    solution.undelivered_count += removed.size();
    solution.all_parcels_delivered = false;

    return removed;
}

std::vector<int> ALNSOperators::destroyWorst(const RoutingInstance& instance, RoutingResult& solution, int num_to_remove) {

    return destroyRandom(instance, solution, num_to_remove); 
}

void ALNSOperators::repairGreedy(const RoutingInstance& instance, RoutingResult& solution, const std::vector<int>& unassigned) {
    std::vector<int> remaining = unassigned;

    for (int pid : remaining) {
        auto it = std::find_if(instance.parcels.begin(), instance.parcels.end(), 
            [pid](const Parcel& p) { return p.id == pid; });
        if (it == instance.parcels.end()) continue;

        const auto& parcel = *it;

        int best_route_idx = -1;
        int best_insert_pos = -1;
        double best_cost_increase = std::numeric_limits<double>::max();

        for (size_t r = 0; r < solution.routes.size(); ++r) {
            const Route& test_route = solution.routes[r];
            const auto& vehicle = instance.vehicles[test_route.vehicle_id];

            for (size_t pos = 1; pos < test_route.location_ids.size(); ++pos) {
                double cost_increase = 0.0;
                if (ConstraintChecker::checkInsertionFeasibility(instance, test_route, vehicle, parcel, pos, cost_increase)) {
                    if (cost_increase < best_cost_increase) {
                        best_cost_increase = cost_increase;
                        best_route_idx = r;
                        best_insert_pos = pos;
                    }
                }
            }
        }

        if (best_route_idx != -1) {
            Route& target = solution.routes[best_route_idx];
            target.parcel_ids.insert(target.parcel_ids.begin() + best_insert_pos - 1, parcel.id);
            target.location_ids.insert(target.location_ids.begin() + best_insert_pos, parcel.location_id);
            ConstraintChecker::evaluateRoute(instance, target, instance.vehicles[target.vehicle_id]);
            solution.undelivered_count--;
        }
    }

    solution.all_parcels_delivered = (solution.undelivered_count == 0);

    solution.total_distance = 0.0;
    solution.total_duration = 0.0;
    for (const auto& r : solution.routes) {
        solution.total_distance += r.route_distance;
        solution.total_duration += r.route_duration;
    }
}

void ALNSOperators::repairRegret(const RoutingInstance& instance, RoutingResult& solution, const std::vector<int>& unassigned) {
    std::vector<int> remaining = unassigned;

    while (!remaining.empty()) {
        int best_parcel_idx = -1;
        double max_regret = -1.0;

        int best_route_idx = -1;
        Route best_modified_route;

        for (size_t i = 0; i < remaining.size(); ++i) {
            int pid = remaining[i];

            auto it = std::find_if(instance.parcels.begin(), instance.parcels.end(), 
                [pid](const Parcel& p) { return p.id == pid; });
            if (it == instance.parcels.end()) continue;

            const Parcel& parcel = *it;

            double best_cost = std::numeric_limits<double>::max();
            double second_best_cost = std::numeric_limits<double>::max();

            int local_best_route = -1;
            Route local_best_modified;

            for (size_t r = 0; r < solution.routes.size(); ++r) {
                const Route& test_route = solution.routes[r];
                const auto& vehicle = instance.vehicles[test_route.vehicle_id];

                double best_for_route = std::numeric_limits<double>::max();

                for (size_t pos = 1; pos < test_route.location_ids.size(); ++pos) {
                    double cost_inc = 0.0;
                    if (ConstraintChecker::checkInsertionFeasibility(instance, test_route, vehicle, parcel, pos, cost_inc)) {
                        if (cost_inc < best_for_route) {
                            best_for_route = cost_inc;
                        }
                    }
                }

                if (best_for_route < best_cost) {
                    second_best_cost = best_cost;
                    best_cost = best_for_route;
                    local_best_route = r;
                } else if (best_for_route < second_best_cost) {
                    second_best_cost = best_for_route;
                }
            }

            if (local_best_route != -1) {
                double regret = (second_best_cost == std::numeric_limits<double>::max()) ? 
                                1000000.0 : (second_best_cost - best_cost);

                if (regret > max_regret) {
                    max_regret = regret;
                    best_parcel_idx = i;
                    best_route_idx = local_best_route;
                }
            }
        }

        if (best_parcel_idx != -1) {
            int pid = remaining[best_parcel_idx];
            const auto& parcel = instance.parcels[instance.parcel_id_to_index.at(pid)];

            double min_inc = std::numeric_limits<double>::max();
            int best_pos = -1;
            Route& target = solution.routes[best_route_idx];
            for (size_t pos = 1; pos < target.location_ids.size(); ++pos) {
                double inc = 0.0;
                if (ConstraintChecker::checkInsertionFeasibility(instance, target, instance.vehicles[target.vehicle_id], parcel, pos, inc)) {
                    if (inc < min_inc) {
                        min_inc = inc;
                        best_pos = pos;
                    }
                }
            }

            target.parcel_ids.insert(target.parcel_ids.begin() + best_pos - 1, parcel.id);
            target.location_ids.insert(target.location_ids.begin() + best_pos, parcel.location_id);
            ConstraintChecker::evaluateRoute(instance, target, instance.vehicles[target.vehicle_id]);

            solution.undelivered_count--;
            remaining.erase(remaining.begin() + best_parcel_idx);
        } else {
            break;
        }
    }

    solution.all_parcels_delivered = (solution.undelivered_count == 0);

    solution.total_distance = 0.0;
    solution.total_duration = 0.0;
    for (const auto& r : solution.routes) {
        solution.total_distance += r.route_distance;
        solution.total_duration += r.route_duration;
    }
}

}
