#include "GASolver.h"
#include "NearestNeighborSolver.h"
#include "ConstraintChecker.h"
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>

namespace routing {

double GASolver::calculateCost(const RoutingInstance& instance, const RoutingResult& result) {
    double cost = result.total_distance;
    cost += result.undelivered_count * PENALTY_PER_UNDELIVERED;
    return cost;
}

RoutingResult GASolver::decode(const RoutingInstance& instance, const std::vector<int>& sequence) {
    RoutingResult result = createResult("GeneticAlgorithm");

    int vehicle_idx = 0;
    Route current_route;

    if (!instance.vehicles.empty()) {
        current_route.vehicle_id = instance.vehicles[vehicle_idx].id;
        current_route.location_ids.push_back(0); 
        current_route.arrival_times.push_back(0.0);
    }

    std::vector<int> undelivered;

    for (int pid : sequence) {
        if (vehicle_idx >= instance.vehicles.size()) {
            undelivered.push_back(pid);
            continue;
        }

        auto it = std::find_if(instance.parcels.begin(), instance.parcels.end(), 
            [pid](const Parcel& p) { return p.id == pid; });
        if (it == instance.parcels.end()) continue;

        const Parcel& parcel = *it;
        const Vehicle& vehicle = instance.vehicles[vehicle_idx];

        Route temp_route = current_route;
        temp_route.parcel_ids.push_back(parcel.id);
        temp_route.location_ids.push_back(parcel.location_id);

        if (ConstraintChecker::evaluateRoute(instance, temp_route, vehicle)) {
            current_route = temp_route;
        } else {

            if (!current_route.parcel_ids.empty()) {

                current_route.location_ids.push_back(0);
                ConstraintChecker::evaluateRoute(instance, current_route, vehicle);
                result.routes.push_back(current_route);
            }

            vehicle_idx++;
            if (vehicle_idx < instance.vehicles.size()) {
                current_route = Route();
                current_route.vehicle_id = instance.vehicles[vehicle_idx].id;
                current_route.location_ids.push_back(0);
                current_route.arrival_times.push_back(0.0);

                temp_route = current_route;
                temp_route.parcel_ids.push_back(parcel.id);
                temp_route.location_ids.push_back(parcel.location_id);
                if (ConstraintChecker::evaluateRoute(instance, temp_route, instance.vehicles[vehicle_idx])) {
                    current_route = temp_route;
                } else {

                    undelivered.push_back(pid);
                }
            } else {
                undelivered.push_back(pid);
            }
        }
    }

    if (!current_route.parcel_ids.empty() && vehicle_idx < instance.vehicles.size()) {
        current_route.location_ids.push_back(0);
        ConstraintChecker::evaluateRoute(instance, current_route, instance.vehicles[vehicle_idx]);
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
    std::mt19937 rng(std::random_device{}());
    int n = p1.parcel_sequence.size();

    if (n < 2) return p1;

    std::uniform_int_distribution<int> dist(0, n - 1);
    int start = dist(rng);
    int end = dist(rng);
    if (start > end) std::swap(start, end);

    Chromosome child;
    child.parcel_sequence.assign(n, -1);

    for (int i = start; i <= end; ++i) {
        child.parcel_sequence[i] = p1.parcel_sequence[i];
    }

    int current_idx = (end + 1) % n;
    for (int i = 0; i < n; ++i) {
        int p2_idx = (end + 1 + i) % n;
        int val = p2.parcel_sequence[p2_idx];

        if (std::find(child.parcel_sequence.begin(), child.parcel_sequence.end(), val) == child.parcel_sequence.end()) {
            child.parcel_sequence[current_idx] = val;
            current_idx = (current_idx + 1) % n;
        }
    }

    return child;
}

void GASolver::mutate(Chromosome& chrom, const RoutingInstance& instance) {
    std::mt19937 rng(std::random_device{}());
    int n = chrom.parcel_sequence.size();
    if (n < 2) return;

    std::uniform_real_distribution<double> prob(0.0, 1.0);
    if (prob(rng) < 0.2) { 
        std::uniform_int_distribution<int> dist(0, n - 1);
        int i1 = dist(rng);
        int i2 = dist(rng);
        std::swap(chrom.parcel_sequence[i1], chrom.parcel_sequence[i2]);
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

    int population_size = 20;
    int max_generations = 50;

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

        auto best_it = std::max_element(population.begin(), population.end(), 
            [](const Chromosome& a, const Chromosome& b) { return a.fitness < b.fitness; });
        new_population.push_back(*best_it);

        if (best_it->fitness > best_overall.fitness) {
            best_overall = *best_it;
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

    auto end_time = std::chrono::high_resolution_clock::now();
    best_overall.result.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return best_overall.result;
}

}
