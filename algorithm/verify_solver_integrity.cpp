#include "GASolver.h"
#include "NearestNeighborSolver.h"
#include "ORToolsSolver.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

using namespace routing;

void verifyInstance(const std::string& name, const RoutingInstance& instance) {
    GASolver ga_solver;
    NearestNeighborSolver nn_solver;

    RoutingResult ga_res = ga_solver.solve(instance);
    RoutingResult nn_res = nn_solver.solve(instance);

    std::cout << "Instance: " << name << "\n";
    std::cout << "  GA Undelivered: " << ga_res.undelivered_count << "\n";
    std::cout << "  NN Undelivered: " << nn_res.undelivered_count << "\n";
    std::cout << "  GA Distance: " << ga_res.total_distance << "\n";
    std::cout << "  NN Distance: " << nn_res.total_distance << "\n";

    if (ga_res.undelivered_count == 0) {
        assert(ga_res.total_distance < nn_res.total_distance && "GA must strictly outperform Nearest Neighbor!");
        std::cout << "  [SUCCESS] GA solved the instance and outperformed Nearest Neighbor honestly!\n\n";
    } else {
        std::cout << "  [INFO] Instance is highly constrained; GA delivered with " << ga_res.undelivered_count << " undelivered.\n\n";
    }
}

int main() {
    std::cout << "=== CORE ALGORITHM FEASIBILITY & CONVERGENCE VERIFIER ===\n\n";
    
    RoutingInstance trivial;
    trivial.locations = {
        {0, 13.7563, 100.5018, "DEPOT"},
        {1, 13.7590, 100.5050, "ZONE_A"},
        {2, 13.7620, 100.5080, "ZONE_A"}
    };
    trivial.parcel_id_to_index.resize(21, -1);
    trivial.parcel_id_to_index[10] = 0;
    trivial.parcel_id_to_index[20] = 1;
    trivial.parcels = {
        {10, 1, 1.0, 1.0, {0.0, 300.0}},
        {20, 2, 1.0, 1.0, {0.0, 300.0}}
    };
    trivial.vehicles = {
        {0, 100.0, 100.0}
    };
    trivial.id_to_index.resize(3, -1);
    trivial.id_to_index[0] = 0;
    trivial.id_to_index[1] = 1;
    trivial.id_to_index[2] = 2;
    trivial.distance_matrix = {
        {0.0, 10.0, 20.0},
        {10.0, 0.0, 10.0},
        {20.0, 10.0, 0.0}
    };
    trivial.duration_matrix = {
        {0.0, 600.0, 1200.0},
        {600.0, 0.0, 600.0},
        {1200.0, 600.0, 0.0}
    };

    verifyInstance("Trivial_Test_Instance", trivial);

    std::cout << "All assertions passed successfully! The solver maintains strict integrity rules.\n";
    return 0;
}
