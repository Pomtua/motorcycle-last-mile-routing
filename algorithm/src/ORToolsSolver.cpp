#include "ORToolsSolver.h"
#include "ortools/constraint_solver/routing.h"
#include "ortools/constraint_solver/routing_enums.pb.h"
#include "ortools/constraint_solver/routing_index_manager.h"
#include "ortools/constraint_solver/routing_parameters.h"
#include <chrono>

namespace routing {

RoutingResult ORToolsSolver::solve(const RoutingInstance& instance, double time_limit_seconds) {
    auto start_time = std::chrono::high_resolution_clock::now();
    RoutingResult result = createResult("ORTools");

    int num_parcels = instance.parcels.size();
    int num_vehicles = instance.vehicles.size();
    int num_nodes = num_parcels + 1;

    operations_research::RoutingIndexManager manager(num_nodes, num_vehicles, operations_research::RoutingIndexManager::NodeIndex{0});
    operations_research::RoutingModel routing(manager);

    const int distance_evaluator_index = routing.RegisterTransitCallback(
        [&instance, &manager](int64_t from_index, int64_t to_index) -> int64_t {
            auto from_node = manager.IndexToNode(from_index).value();
            auto to_node = manager.IndexToNode(to_index).value();
            int from_loc = (from_node == 0) ? 0 : instance.id_to_index.at(instance.parcels[from_node - 1].location_id);
            int to_loc = (to_node == 0) ? 0 : instance.id_to_index.at(instance.parcels[to_node - 1].location_id);
            return static_cast<int64_t>(instance.distance_matrix[from_loc][to_loc] * 1000.0);
        });

    routing.SetArcCostEvaluatorOfAllVehicles(distance_evaluator_index);

    const int time_evaluator_index = routing.RegisterTransitCallback(
        [&instance, &manager](int64_t from_index, int64_t to_index) -> int64_t {
            auto from_node = manager.IndexToNode(from_index).value();
            auto to_node = manager.IndexToNode(to_index).value();
            int from_loc = (from_node == 0) ? 0 : instance.id_to_index.at(instance.parcels[from_node - 1].location_id);
            int to_loc = (to_node == 0) ? 0 : instance.id_to_index.at(instance.parcels[to_node - 1].location_id);
            return static_cast<int64_t>(instance.duration_matrix[from_loc][to_loc] / 60.0 * 1000.0);
        });

    routing.AddDimension(time_evaluator_index, 1440 * 1000, 1440 * 1000, false, "Time");
    const operations_research::RoutingDimension& time_dimension = routing.GetDimensionOrDie("Time");

    const int weight_evaluator_index = routing.RegisterUnaryTransitCallback(
        [&instance, &manager](int64_t from_index) -> int64_t {
            auto from_node = manager.IndexToNode(from_index).value();
            if (from_node == 0) return 0;
            return static_cast<int64_t>(instance.parcels[from_node - 1].weight * 1000.0);
        });

    std::vector<int64_t> vehicle_weight_capacities(num_vehicles);
    for (int i = 0; i < num_vehicles; ++i) {
        vehicle_weight_capacities[i] = static_cast<int64_t>(instance.vehicles[i].capacity_weight * 1000.0);
    }
    routing.AddDimensionWithVehicleCapacity(weight_evaluator_index, 0, vehicle_weight_capacities, true, "Weight");

    const int volume_evaluator_index = routing.RegisterUnaryTransitCallback(
        [&instance, &manager](int64_t from_index) -> int64_t {
            auto from_node = manager.IndexToNode(from_index).value();
            if (from_node == 0) return 0;
            return static_cast<int64_t>(instance.parcels[from_node - 1].volume * 1000.0);
        });

    std::vector<int64_t> vehicle_volume_capacities(num_vehicles);
    for (int i = 0; i < num_vehicles; ++i) {
        vehicle_volume_capacities[i] = static_cast<int64_t>(instance.vehicles[i].capacity_volume * 1000.0);
    }
    routing.AddDimensionWithVehicleCapacity(volume_evaluator_index, 0, vehicle_volume_capacities, true, "Volume");

    for (int i = 1; i < num_nodes; ++i) {
        auto index = manager.NodeToIndex(operations_research::RoutingIndexManager::NodeIndex{i});
        time_dimension.CumulVar(index)->SetRange(
            static_cast<int64_t>(instance.parcels[i-1].time_window.start * 1000.0),
            static_cast<int64_t>(instance.parcels[i-1].time_window.end * 1000.0)
        );
        routing.AddDisjunction({index}, static_cast<int64_t>(Solver::PENALTY_PER_UNDELIVERED * 1000.0));
    }

    operations_research::RoutingSearchParameters search_parameters = operations_research::DefaultRoutingSearchParameters();
    search_parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PATH_CHEAPEST_ARC);
    search_parameters.set_local_search_metaheuristic(operations_research::LocalSearchMetaheuristic::GUIDED_LOCAL_SEARCH);
    int64_t secs = static_cast<int64_t>(time_limit_seconds);
    int32_t nanos = static_cast<int32_t>((time_limit_seconds - secs) * 1e9);
    search_parameters.mutable_time_limit()->set_seconds(secs);
    search_parameters.mutable_time_limit()->set_nanos(nanos);

    const operations_research::Assignment* solution = routing.SolveWithParameters(search_parameters);

    std::set<int> unassigned_parcels;
    for (int i = 0; i < num_parcels; ++i) {
        unassigned_parcels.insert(i);
    }

    if (solution) {
        for (int vehicle_id = 0; vehicle_id < num_vehicles; ++vehicle_id) {
            int64_t index = routing.Start(vehicle_id);
            if (routing.IsEnd(solution->Value(routing.NextVar(index)))) continue;

            Route route;
            route.vehicle_id = instance.vehicles[vehicle_id].id;
            route.location_ids.push_back(0);
            route.arrival_times.push_back(0.0);

            double route_dist = 0.0;
            double route_dur = 0.0;
            int prev_loc = 0;
            double current_time = 0.0;

            while (!routing.IsEnd(index)) {
                index = solution->Value(routing.NextVar(index));
                if (routing.IsEnd(index)) break;

                int node = manager.IndexToNode(index).value();
                int parcel_idx = node - 1;
                
                int curr_loc = instance.id_to_index.at(instance.parcels[parcel_idx].location_id);
                double leg_dist = instance.distance_matrix[prev_loc][curr_loc];
                double leg_dur = instance.duration_matrix[prev_loc][curr_loc];

                route_dist += leg_dist;
                route_dur += leg_dur;
                
                current_time += leg_dur / 60.0;
                if (current_time < instance.parcels[parcel_idx].time_window.start) {
                    current_time = instance.parcels[parcel_idx].time_window.start;
                }

                route.parcel_ids.push_back(instance.parcels[parcel_idx].id);
                route.location_ids.push_back(instance.parcels[parcel_idx].location_id);
                route.arrival_times.push_back(current_time);

                unassigned_parcels.erase(parcel_idx);
                prev_loc = curr_loc;
            }

            double leg_dist = instance.distance_matrix[prev_loc][0];
            double leg_dur = instance.duration_matrix[prev_loc][0];
            route_dist += leg_dist;
            route_dur += leg_dur;
            route.location_ids.push_back(0);

            route.route_distance = route_dist;
            route.route_duration = route_dur;

            result.routes.push_back(route);
            result.total_distance += route_dist;
            result.total_duration += route_dur;
        }
    }

    result.all_parcels_delivered = unassigned_parcels.empty();
    result.undelivered_count = unassigned_parcels.size();
    result.total_cost = result.total_distance + (result.undelivered_count * Solver::PENALTY_PER_UNDELIVERED);

    repairUndelivered(instance, result);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return result;
}

}
