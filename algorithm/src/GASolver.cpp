#include "GASolver.h"
#include "NearestNeighborSolver.h"
#include "ConstraintChecker.h"
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <set>

namespace routing {

double GASolver::calculateCost(const RoutingInstance& instance, const RoutingResult& result) {
    double cost = result.total_distance;
    cost += result.undelivered_count * 1000000.0;
    return cost;
}

RoutingResult GASolver::decode(const RoutingInstance& instance, const std::vector<int>& sequence) {
    RoutingResult result = createResult("GeneticAlgorithm");

    int vehicle_idx = 0;
    Route current_route;

    double current_weight = 0.0;
    double current_volume = 0.0;
    double current_time = 0.0;
    std::string route_zone = "";

    if (!instance.vehicles.empty()) {
        current_route.vehicle_id = instance.vehicles[vehicle_idx].id;
        current_route.location_ids.push_back(0); 
        current_route.arrival_times.push_back(0.0);
    }

    std::vector<int> undelivered;

    for (int pid : sequence) {
        if (vehicle_idx >= (int)instance.vehicles.size()) {
            undelivered.push_back(pid);
            continue;
        }

        if (pid < 0 || pid >= (int)instance.parcel_id_to_index.size()) continue;
        int p_idx = instance.parcel_id_to_index[pid];
        if (p_idx < 0) continue;
        const Parcel& parcel = instance.parcels[p_idx];
        const Vehicle& vehicle = instance.vehicles[vehicle_idx];

        bool cap_ok = (current_weight + parcel.weight <= vehicle.capacity_weight &&
                        current_volume + parcel.volume <= vehicle.capacity_volume);

        std::string p_zone = instance.locations[instance.id_to_index.at(parcel.location_id)].zone_id;
        std::string next_zone = route_zone;
        bool zone_ok = true;
        if (p_zone != "ZONE_UNK" && p_zone != "DEPOT") {
            if (route_zone.empty()) {
                next_zone = p_zone;
            } else if (p_zone != route_zone) {
                zone_ok = false;
            }
        }

        int current_loc = current_route.location_ids.back();
        int idx1 = instance.id_to_index.at(current_loc);
        int idx2 = instance.id_to_index.at(parcel.location_id);
        double travel_time_min = instance.duration_matrix[idx1][idx2] / 60.0;
        double next_time = current_time + travel_time_min;
        if (next_time < parcel.time_window.start) {
            next_time = parcel.time_window.start;
        }
        bool time_ok = (next_time <= parcel.time_window.end);

        if (cap_ok && zone_ok && time_ok) {
            current_route.parcel_ids.push_back(parcel.id);
            current_route.location_ids.push_back(parcel.location_id);
            current_route.arrival_times.push_back(next_time);
            current_route.route_distance += instance.distance_matrix[idx1][idx2];
            current_route.route_duration += instance.duration_matrix[idx1][idx2];

            current_weight += parcel.weight;
            current_volume += parcel.volume;
            current_time = next_time;
            route_zone = next_zone;
        } else {
            if (!current_route.parcel_ids.empty()) {
                int last_loc = current_route.location_ids.back();
                int idx_last = instance.id_to_index.at(last_loc);
                current_route.route_distance += instance.distance_matrix[idx_last][0];
                current_route.route_duration += instance.duration_matrix[idx_last][0];
                current_route.location_ids.push_back(0);
                
                current_route.total_weight = current_weight;
                current_route.total_volume = current_volume;
                result.routes.push_back(current_route);
            }

            vehicle_idx++;
            if (vehicle_idx < (int)instance.vehicles.size()) {
                const Vehicle& new_vehicle = instance.vehicles[vehicle_idx];
                current_route = Route();
                current_route.vehicle_id = new_vehicle.id;
                current_route.location_ids.push_back(0);
                current_route.arrival_times.push_back(0.0);

                current_weight = 0.0;
                current_volume = 0.0;
                current_time = 0.0;
                route_zone = "";

                if (parcel.weight <= new_vehicle.capacity_weight && parcel.volume <= new_vehicle.capacity_volume) {
                    int idx_dest = instance.id_to_index.at(parcel.location_id);
                    double travel_time = instance.duration_matrix[0][idx_dest] / 60.0;
                    double arr_time = travel_time;
                    if (arr_time < parcel.time_window.start) {
                        arr_time = parcel.time_window.start;
                    }

                    std::string p_zone_new = instance.locations[idx_dest].zone_id;
                    std::string new_zone = "";
                    if (p_zone_new != "ZONE_UNK" && p_zone_new != "DEPOT") {
                        new_zone = p_zone_new;
                    }

                    if (arr_time <= parcel.time_window.end) {
                        current_route.parcel_ids.push_back(parcel.id);
                        current_route.location_ids.push_back(parcel.location_id);
                        current_route.arrival_times.push_back(arr_time);
                        current_route.route_distance = instance.distance_matrix[0][idx_dest];
                        current_route.route_duration = instance.duration_matrix[0][idx_dest];

                        current_weight = parcel.weight;
                        current_volume = parcel.volume;
                        current_time = arr_time;
                        route_zone = new_zone;
                    } else {
                        undelivered.push_back(pid);
                    }
                } else {
                    undelivered.push_back(pid);
                }
            } else {
                undelivered.push_back(pid);
            }
        }
    }

    if (!current_route.parcel_ids.empty() && vehicle_idx < (int)instance.vehicles.size()) {
        int last_loc = current_route.location_ids.back();
        int idx_last = instance.id_to_index.at(last_loc);
        current_route.route_distance += instance.distance_matrix[idx_last][0];
        current_route.route_duration += instance.duration_matrix[idx_last][0];
        current_route.location_ids.push_back(0);
        
        current_route.total_weight = current_weight;
        current_route.total_volume = current_volume;
        result.routes.push_back(current_route);
    }

    result.undelivered_count = undelivered.size();
    result.all_parcels_delivered = undelivered.empty();

    result.total_distance = 0.0;
    result.total_duration = 0.0;
    for (const auto& r : result.routes) {
        result.total_distance += r.route_distance;
        result.total_duration += r.route_duration;
    }

    result.total_cost = calculateCost(instance, result);
    return result;
}

Chromosome GASolver::crossover(const Chromosome& p1, const Chromosome& p2, const RoutingInstance& instance) {
    thread_local std::mt19937 rng(std::random_device{}());
    int n = p1.parcel_sequence.size();

    if (n < 2) return p1;

    std::uniform_int_distribution<int> dist(0, n - 1);
    int start = dist(rng);
    int end = dist(rng);
    if (start > end) std::swap(start, end);

    Chromosome child;
    child.parcel_sequence.assign(n, -1);

    std::vector<bool> in_child(instance.parcel_id_to_index.size(), false);
    for (int i = start; i <= end; ++i) {
        child.parcel_sequence[i] = p1.parcel_sequence[i];
        int val = p1.parcel_sequence[i];
        if (val >= 0 && val < (int)in_child.size()) {
            in_child[val] = true;
        }
    }

    int current_idx = (end + 1) % n;
    for (int i = 0; i < n; ++i) {
        int p2_idx = (end + 1 + i) % n;
        int val = p2.parcel_sequence[p2_idx];

        if (val >= 0 && val < (int)in_child.size() && !in_child[val]) {
            child.parcel_sequence[current_idx] = val;
            in_child[val] = true;
            current_idx = (current_idx + 1) % n;
        }
    }

    return child;
}

void GASolver::mutate(Chromosome& chrom, const RoutingInstance& instance) {
    thread_local std::mt19937 rng(std::random_device{}());
    int n = chrom.parcel_sequence.size();
    if (n < 2) return;

    std::uniform_real_distribution<double> prob(0.0, 1.0);
    if (prob(rng) < 0.25 && n <= 100) {
        int ruin_size = std::max(1, n / 10);
        std::uniform_int_distribution<int> dist_idx(0, n - ruin_size - 1);
        int start_idx = dist_idx(rng);

        std::vector<int> ruined_pids;
        for (int i = 0; i < ruin_size; ++i) {
            ruined_pids.push_back(chrom.parcel_sequence[start_idx]);
            chrom.parcel_sequence.erase(chrom.parcel_sequence.begin() + start_idx);
        }

        for (int pid : ruined_pids) {
            double best_cost_inc = std::numeric_limits<double>::max();
            int best_insert_pos = 0;

            for (size_t pos = 0; pos <= chrom.parcel_sequence.size(); ++pos) {
                std::vector<int> test_seq = chrom.parcel_sequence;
                test_seq.insert(test_seq.begin() + pos, pid);
                RoutingResult test_res = decode(instance, test_seq);
                if (test_res.undelivered_count == 0 && test_res.total_distance < best_cost_inc) {
                    best_cost_inc = test_res.total_distance;
                    best_insert_pos = pos;
                }
            }
            chrom.parcel_sequence.insert(chrom.parcel_sequence.begin() + best_insert_pos, pid);
        }
    } else {
        std::uniform_int_distribution<int> dist(0, n - 1);
        int i1 = dist(rng);
        int i2 = dist(rng);
        std::swap(chrom.parcel_sequence[i1], chrom.parcel_sequence[i2]);
    }
}

static void optimizeRoute2Opt(const RoutingInstance& instance, Route& route, const Vehicle& vehicle) {
    if (route.parcel_ids.size() < 2) return;
    bool improved = true;
    while (improved) {
        improved = false;
        for (size_t i = 0; i < route.parcel_ids.size(); ++i) {
            for (size_t j = i + 1; j < route.parcel_ids.size(); ++j) {
                Route test_route = route;
                std::reverse(test_route.parcel_ids.begin() + i, test_route.parcel_ids.begin() + j + 1);
                test_route.location_ids.clear();
                test_route.location_ids.push_back(0);
                for (int pid : test_route.parcel_ids) {
                    const auto& p = instance.parcels[instance.parcel_id_to_index.at(pid)];
                    test_route.location_ids.push_back(p.location_id);
                }
                test_route.location_ids.push_back(0);
                if (ConstraintChecker::evaluateRoute(instance, test_route, vehicle)) {
                    if (test_route.route_distance < route.route_distance - 0.001) {
                        route = test_route;
                        improved = true;
                    }
                }
            }
        }
    }
}

static void optimizeInterRouteRelocate(const RoutingInstance& instance, RoutingResult& result) {
    bool improved = true;
    while (improved) {
        improved = false;
        for (size_t r1 = 0; r1 < result.routes.size(); ++r1) {
            for (size_t r2 = 0; r2 < result.routes.size(); ++r2) {
                if (r1 == r2) continue;
                Route& route1 = result.routes[r1];
                Route& route2 = result.routes[r2];
                if (route1.parcel_ids.empty()) continue;
                const Vehicle& v1 = instance.vehicles[route1.vehicle_id];
                const Vehicle& v2 = instance.vehicles[route2.vehicle_id];

                for (size_t i = 0; i < route1.parcel_ids.size(); ++i) {
                    int pid = route1.parcel_ids[i];
                    const Parcel& p = instance.parcels[instance.parcel_id_to_index.at(pid)];
                    
                    for (size_t pos = 1; pos <= route2.location_ids.size() - 1; ++pos) {
                        double cost_inc = 0.0;
                        if (ConstraintChecker::checkInsertionFeasibility(instance, route2, v2, p, pos, cost_inc)) {
                            double saving = (route1.route_distance + route2.route_distance);
                            
                            Route test_r1 = route1;
                            test_r1.parcel_ids.erase(test_r1.parcel_ids.begin() + i);
                            test_r1.location_ids.erase(test_r1.location_ids.begin() + i + 1);
                            ConstraintChecker::evaluateRoute(instance, test_r1, v1);

                            Route test_r2 = route2;
                            test_r2.parcel_ids.insert(test_r2.parcel_ids.begin() + pos - 1, pid);
                            test_r2.location_ids.insert(test_r2.location_ids.begin() + pos, p.location_id);
                            ConstraintChecker::evaluateRoute(instance, test_r2, v2);

                            if (test_r1.route_distance + test_r2.route_distance < saving - 0.001) {
                                route1 = test_r1;
                                route2 = test_r2;
                                improved = true;
                                break;
                            }
                        }
                    }
                    if (improved) break;
                }
                if (improved) break;
            }
            if (improved) break;
        }
    }
}

RoutingResult GASolver::solve(const RoutingInstance& instance) {
    auto start_time = std::chrono::high_resolution_clock::now();

    NearestNeighborSolver nn_solver;
    RoutingResult nn_result = nn_solver.solve(instance);

    std::vector<int> nn_sequence;
    for (const auto& route : nn_result.routes) {
        for (int pid : route.parcel_ids) {
            nn_sequence.push_back(pid);
        }
    }

    std::vector<int> all_pids;
    for (const auto& p : instance.parcels) {
        all_pids.push_back(p.id);
        if (std::find(nn_sequence.begin(), nn_sequence.end(), p.id) == nn_sequence.end()) {
            nn_sequence.push_back(p.id);
        }
    }

    int population_size = 100;
    int max_generations = 200;
    int n_parcels = instance.parcels.size();
    if (n_parcels <= 50) {
        max_generations = 40;
    } else if (n_parcels <= 100) {
        max_generations = 80;
    } else if (n_parcels <= 200) {
        max_generations = 120;
    }

    std::vector<Chromosome> population;
    std::mt19937 rng(42);

    Chromosome seed;
    seed.parcel_sequence = nn_sequence;
    seed.result = decode(instance, seed.parcel_sequence);
    seed.fitness = 1.0 / (seed.result.total_cost + 1.0);
    population.push_back(seed);

    for (int i = 1; i < population_size; ++i) {
        Chromosome c;
        c.parcel_sequence = all_pids;
        std::shuffle(c.parcel_sequence.begin(), c.parcel_sequence.end(), rng);
        c.result = decode(instance, c.parcel_sequence);
        c.fitness = 1.0 / (c.result.total_cost + 1.0);
        population.push_back(c);
    }

    Chromosome best_overall = population[0];

    for (int gen = 0; gen < max_generations; ++gen) {
        std::vector<Chromosome> new_population;

        std::sort(population.begin(), population.end(), 
            [](const Chromosome& a, const Chromosome& b) { return a.fitness > b.fitness; });

        auto& best_chrom = population[0];
        if (gen % 20 == 0) {
            best_chrom.result = decode(instance, best_chrom.parcel_sequence);
            for (auto& route : best_chrom.result.routes) {
                if (route.vehicle_id < (int)instance.vehicles.size()) {
                    const Vehicle& vehicle = instance.vehicles[route.vehicle_id];
                    optimizeRoute2Opt(instance, route, vehicle);
                }
            }
            std::vector<int> opt_seq;
            for (const auto& route : best_chrom.result.routes) {
                for (int pid : route.parcel_ids) {
                    opt_seq.push_back(pid);
                }
            }
            std::set<int> delivered_set(opt_seq.begin(), opt_seq.end());
            for (int pid : best_chrom.parcel_sequence) {
                if (!delivered_set.count(pid)) {
                    opt_seq.push_back(pid);
                }
            }
            best_chrom.parcel_sequence = opt_seq;
            best_chrom.result.total_distance = 0.0;
            best_chrom.result.total_duration = 0.0;
            for (const auto& r : best_chrom.result.routes) {
                best_chrom.result.total_distance += r.route_distance;
                best_chrom.result.total_duration += r.route_duration;
            }
            best_chrom.result.total_cost = calculateCost(instance, best_chrom.result);
            best_chrom.fitness = 1.0 / (best_chrom.result.total_cost + 1.0);
        }

        if (best_chrom.fitness > best_overall.fitness) {
            best_overall = best_chrom;
        }

        int elitism_size = 10;
        for (int i = 0; i < elitism_size && i < (int)population.size(); ++i) {
            new_population.push_back(population[i]);
        }

        std::uniform_int_distribution<int> pop_dist(0, population_size - 1);

        while (new_population.size() < population_size) {

            int t1 = pop_dist(rng);
            int t2 = pop_dist(rng);
            const Chromosome& p1 = population[t1].fitness > population[t2].fitness ? population[t1] : population[t2];

            t1 = pop_dist(rng);
            t2 = pop_dist(rng);
            const Chromosome& p2 = population[t1].fitness > population[t2].fitness ? population[t1] : population[t2];

            Chromosome child = crossover(p1, p2, instance);
            mutate(child, instance);

            child.result = decode(instance, child.parcel_sequence);
            child.fitness = 1.0 / (child.result.total_cost + 1.0);

            new_population.push_back(child);
        }

        population = new_population;
    }

    repairUndelivered(instance, best_overall.result);

    if (n_parcels <= 200) {
        optimizeInterRouteRelocate(instance, best_overall.result);
    }

    for (auto& route : best_overall.result.routes) {
        if (route.vehicle_id < (int)instance.vehicles.size()) {
            const Vehicle& vehicle = instance.vehicles[route.vehicle_id];
            optimizeRoute2Opt(instance, route, vehicle);
        }
    }

    if (n_parcels <= 200) {
        optimizeInterRouteRelocate(instance, best_overall.result);
    }

    for (auto& route : best_overall.result.routes) {
        if (route.vehicle_id < (int)instance.vehicles.size()) {
            const Vehicle& vehicle = instance.vehicles[route.vehicle_id];
            bool valid = ConstraintChecker::evaluateRoute(instance, route, vehicle);
            if (!valid) {
                best_overall.result.undelivered_count += (int)route.parcel_ids.size();
                route.parcel_ids.clear();
                route.location_ids = {0};
                route.route_distance = 0.0;
                route.route_duration = 0.0;
            }
        }
    }
    best_overall.result.total_distance = 0.0;
    best_overall.result.total_duration = 0.0;
    for (const auto& r : best_overall.result.routes) {
        best_overall.result.total_distance += r.route_distance;
        best_overall.result.total_duration += r.route_duration;
    }
    best_overall.result.all_parcels_delivered = (best_overall.result.undelivered_count == 0);
    best_overall.result.total_cost = calculateCost(instance, best_overall.result);

    auto end_time = std::chrono::high_resolution_clock::now();
    best_overall.result.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return best_overall.result;
}

}
