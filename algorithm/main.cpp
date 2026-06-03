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
#include "ORToolsSolver.h"
#include <nlohmann/json.hpp>

void printResultJson(const routing::RoutingResult& result) {
    nlohmann::json j;
    j["solver_name"] = result.solver_name;
    j["status"] = result.all_parcels_delivered ? "SUCCESS" : "INCOMPLETE";
    j["undelivered_count"] = result.undelivered_count;
    j["total_distance"] = result.total_distance / 1000.0;
    j["total_cost"] = result.total_cost / 1000.0;
    j["total_duration"] = result.total_duration / 60.0;
    j["execution_time_ms"] = result.execution_time_ms;
    j["number_of_routes"] = result.routes.size();
    
    nlohmann::json routes_json = nlohmann::json::array();
    for (const auto& route : result.routes) {
        nlohmann::json r;
        r["vehicle_id"] = route.vehicle_id;
        r["parcel_count"] = route.parcel_ids.size();
        r["route_distance"] = route.route_distance / 1000.0;
        r["route_duration"] = route.route_duration;
        
        nlohmann::json sequence = nlohmann::json::array();
        for(size_t i = 0; i < route.location_ids.size(); ++i) {
            nlohmann::json node;
            node["location_id"] = route.location_ids[i];
            if(i > 0 && i - 1 < route.parcel_ids.size()) {
                node["parcel_id"] = route.parcel_ids[i - 1];
            } else {
                node["parcel_id"] = nullptr;
            }
            node["arrival_time"] = route.arrival_times.size() > i ? route.arrival_times[i] : 0.0;
            sequence.push_back(node);
        }
        r["sequence"] = sequence;
        routes_json.push_back(r);
    }
    j["routes"] = routes_json;
    
    std::cout << j.dump(4) << std::endl;
}

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

int main(int argc, char* argv[]) {
    try {
        std::string test_file = "../../data/instances/split_capability/split_50_1.json";
        bool json_output = false;
        std::string solver_type = "ALL";

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--json") {
                json_output = true;
            } else if (arg == "--solver" && i + 1 < argc) {
                solver_type = argv[++i];
            } else {
                test_file = arg;
            }
        }
        
        routing::RoutingInstance instance = routing::JsonParser::parse(test_file);
        std::vector<routing::Parcel> original_parcels = instance.parcels;
        std::vector<int> original_parcel_id_to_index = instance.parcel_id_to_index;

        routing::SplitDeliveryProcessor::process(instance);

        routing::OsrmClient osrm("localhost", 5000);

        std::string filename = test_file.substr(test_file.find_last_of("/\\") + 1);
        std::string cache_key = filename; 
        osrm.fillMatrices(instance, cache_key);

        routing::RoutingInstance unsplit_instance = instance;
        unsplit_instance.parcels = original_parcels;
        unsplit_instance.parcel_id_to_index = original_parcel_id_to_index;

        nlohmann::json all_results = nlohmann::json::array();

        if (solver_type == "ALL" || solver_type == "NN") {
            routing::NearestNeighborSolver nn_solver;
            auto result_nn = nn_solver.solve(instance);
            if (json_output) {
                printResultJson(result_nn);
            } else {
                printResult(result_nn);
            }
        }

        if (solver_type == "ALL" || solver_type == "ALNS") {
            routing::ALNSSolver alns_solver;
            auto result_alns = alns_solver.solve(instance);
            if (json_output) {
                printResultJson(result_alns);
            } else {
                printResult(result_alns);
            }
        }

        if (solver_type == "ALL" || solver_type == "GA") {
            routing::GASolver ga_solver;
            auto result_ga = ga_solver.solve(instance);
            if (json_output) {
                printResultJson(result_ga);
            } else {
                printResult(result_ga);
            }
        }

        if (solver_type == "ALL" || solver_type == "ORTools" || solver_type == "ORT") {
            routing::ORToolsSolver ort_solver;
            auto result_ort = ort_solver.solve(unsplit_instance);
            if (json_output) {
                printResultJson(result_ort);
            } else {
                printResult(result_ort);
            }
        }

    } catch (const std::exception& e) {
        if(argc > 1 && std::string(argv[1]) == "--json") {
            std::cerr << "{\"error\": \"" << e.what() << "\"}" << std::endl;
        } else {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        return 1;
    }

    return 0;
}
