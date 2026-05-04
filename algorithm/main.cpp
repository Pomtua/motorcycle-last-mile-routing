#include <iostream>
#include <iomanip>
#include "RoutingInstance.h"
#include "JsonParser.h"
#include "OsrmClient.h"
#include "NearestNeighborSolver.h"
#include "SplitDeliveryProcessor.h"
#include "KMeansClusterer.h"
#include "ALNSSolver.h"
#include "GASolver.h"

void printResult(const routing::RoutingResult& result) {
    std::cout << "\n--- Solver: " << result.solver_name << " ---" << std::endl;
    std::cout << "Status: " << (result.all_parcels_delivered ? "SUCCESS" : "INCOMPLETE") << std::endl;
    std::cout << "Undelivered: " << result.undelivered_count << " parcels" << std::endl;
    std::cout << "Total Distance: " << std::fixed << std::setprecision(2) << result.total_distance / 1000.0 << " km" << std::endl;
    std::cout << "Total Cost (Penalty): " << result.total_cost / 1000.0 << " (Distance + Penalty)" << std::endl;
    std::cout << "Total Duration: " << result.total_duration / 60.0 << " min" << std::endl;
    std::cout << "Execution Time: " << result.execution_time_ms << " ms" << std::endl;
    std::cout << "Number of Routes: " << result.routes.size() << std::endl;

    for (const auto& route : result.routes) {
        std::cout << "  Vehicle " << route.vehicle_id << ": " << route.parcel_ids.size() << " parcels, " 
                  << route.route_distance / 1000.0 << " km" << std::endl;
    }
}

int main() {
    std::cout << "Motorcycle Routing Engine Initialized" << std::endl;

    try {
        std::string test_file = "../../data/instances/split_capability/split_50_1.json";
        routing::RoutingInstance instance = routing::JsonParser::parse(test_file);

        std::cout << "Successfully parsed: " << test_file << std::endl;

        routing::SplitDeliveryProcessor::process(instance);
        std::cout << "After split processing, total parcels to deliver: " << instance.parcels.size() << std::endl;

        routing::KMeansClusterer::process(instance);
        std::cout << "Clustering complete." << std::endl;

        routing::OsrmClient osrm("localhost", 5000);
        std::cout << "Fetching matrices from OSRM..." << std::endl;

        std::string cache_key = "split_50_1.json"; 
        osrm.fillMatrices(instance, cache_key);

        routing::NearestNeighborSolver nn_solver;
        auto result_nn = nn_solver.solve(instance);
        printResult(result_nn);

        routing::ALNSSolver alns_solver;
        auto result_alns = alns_solver.solve(instance);
        printResult(result_alns);

        routing::GASolver ga_solver;
        auto result_ga = ga_solver.solve(instance);
        printResult(result_ga);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
