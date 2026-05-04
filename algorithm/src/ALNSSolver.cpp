#include "ALNSSolver.h"
#include "NearestNeighborSolver.h"
#include "ALNSOperators.h"
#include <iostream>
#include <cmath>
#include <random>

namespace routing {

double ALNSSolver::calculateCost(const RoutingInstance& instance, const RoutingResult& result) {
    double cost = result.total_distance;
    cost += result.undelivered_count * PENALTY_PER_UNDELIVERED;
    return cost;
}

RoutingResult ALNSSolver::solve(const RoutingInstance& instance) {
    auto start_time = std::chrono::high_resolution_clock::now();
    RoutingResult result = createResult("ALNS");

    NearestNeighborSolver nn_solver;
    RoutingResult current_solution = nn_solver.solve(instance);
    current_solution.solver_name = "ALNS";

    RoutingResult best_solution = current_solution;
    double current_cost = calculateCost(instance, current_solution);
    double best_cost = current_cost;

    double temperature = 1000.0;
    double cooling_rate = 0.995;
    int max_iterations = 1000; 

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> rand_real(0.0, 1.0);
    std::uniform_int_distribution<int> rand_op(0, 1);

    int num_to_remove = std::max(2, (int)(instance.parcels.size() * 0.2)); 

    double destroy_time_ms = 0;
    double repair_time_ms = 0;
    double eval_time_ms = 0;

    for (int iter = 0; iter < max_iterations; ++iter) {
        RoutingResult temp_solution = current_solution;

        auto t1 = std::chrono::high_resolution_clock::now();
        std::vector<int> unassigned;
        if (rand_op(rng) == 0) {
            unassigned = ALNSOperators::destroyRandom(instance, temp_solution, num_to_remove);
        } else {
            unassigned = ALNSOperators::destroyRandom(instance, temp_solution, num_to_remove); 
        }
        auto t2 = std::chrono::high_resolution_clock::now();
        destroy_time_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();

        if (rand_op(rng) == 0) {
            ALNSOperators::repairGreedy(instance, temp_solution, unassigned);
        } else {
            ALNSOperators::repairRegret(instance, temp_solution, unassigned);
        }
        auto t3 = std::chrono::high_resolution_clock::now();
        repair_time_ms += std::chrono::duration<double, std::milli>(t3 - t2).count();

        double temp_cost = calculateCost(instance, temp_solution);

        if (temp_cost < current_cost || std::exp((current_cost - temp_cost) / temperature) > rand_real(rng)) {
            current_solution = temp_solution;
            current_cost = temp_cost;

            if (current_cost < best_cost) {
                best_solution = current_solution;
                best_cost = current_cost;
            }
        }
        auto t4 = std::chrono::high_resolution_clock::now();
        eval_time_ms += std::chrono::duration<double, std::milli>(t4 - t3).count();

        temperature *= cooling_rate;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    best_solution.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "      [ALNS Detailed] Destroy: " << destroy_time_ms << "ms | Repair: " << repair_time_ms << "ms | Accept/Eval: " << eval_time_ms << "ms" << std::endl;

    return best_solution;
}

}
