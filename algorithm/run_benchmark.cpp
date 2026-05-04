#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include "RoutingInstance.h"
#include "JsonParser.h"
#include "OsrmClient.h"
#include "NearestNeighborSolver.h"
#include "SplitDeliveryProcessor.h"
#include "KMeansClusterer.h"
#include "ALNSSolver.h"
#include "GASolver.h"

namespace fs = std::filesystem;

struct Timer {
    std::chrono::steady_clock::time_point start;
    Timer() { start = std::chrono::steady_clock::now(); }
    double elapsedMs() {
        auto now = std::chrono::steady_clock::now();
        return std::max(0.0, std::chrono::duration<double, std::milli>(now - start).count());
    }
};

std::mutex csv_mutex;
std::mutex cout_mutex;

void processInstance(const std::string& filepath, int current, int total, std::ofstream& csv_file) {
    std::string filename = fs::path(filepath).filename().string();

    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "[" << current << "/" << total << "] Processing: " << filename << std::endl;
    }

    try {
        Timer t_total;

        Timer t_prep;
        routing::RoutingInstance instance = routing::JsonParser::parse(filepath);
        routing::SplitDeliveryProcessor::process(instance);
        routing::KMeansClusterer::process(instance);
        double prep_ms = t_prep.elapsedMs();

        std::string filename = fs::path(filepath).filename().string();

        Timer t_osrm;
        routing::OsrmClient osrm("localhost", 5000);
        osrm.fillMatrices(instance, filename);
        double osrm_ms = t_osrm.elapsedMs();

        Timer t_nn;
        routing::NearestNeighborSolver nn_solver;
        auto nn_result = nn_solver.solve(instance);
        double nn_ms = t_nn.elapsedMs();

        Timer t_ga;
        routing::GASolver ga_solver;
        auto ga_result = ga_solver.solve(instance);
        double ga_ms = t_ga.elapsedMs();

        double total_ms = t_total.elapsedMs();

        {
            std::lock_guard<std::mutex> lock(csv_mutex);
            csv_file << filename << "," 
                     << nn_result.undelivered_count << "," << nn_result.total_cost << ","
                     << ga_result.undelivered_count << "," << ga_result.total_cost << ","
                     << prep_ms << "," << osrm_ms << "," << nn_ms << "," << ga_ms << "\n";

            csv_file.flush();
        }

        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "    [" << filename << "] Prep: " << std::fixed << std::setprecision(2) << prep_ms << "ms"
                      << " | OSRM: " << osrm_ms << "ms"
                      << " | NN: " << nn_ms << "ms"
                      << " | GA: " << ga_ms << "ms"
                      << " | Total: " << total_ms << "ms" << std::endl;
        }

    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cerr << "    [ERROR in " << filename << "] " << e.what() << std::endl;
    }
}

int main() {
    std::string base_dir = "../../data/instances";

    std::vector<std::string> json_files;
    if (!fs::exists(base_dir)) {

        base_dir = "data/instances";
        if (!fs::exists(base_dir)) {
            std::cerr << "Error: Could not find instances directory at ../../data/instances or data/instances" << std::endl;
            return 1;
        }
    }

    for (const auto& entry : fs::recursive_directory_iterator(base_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            json_files.push_back(entry.path().string());
        }
    }

    int total = json_files.size();
    std::cout << "Found " << total << " instances to process." << std::endl;

    std::ofstream csv_file("benchmark_results.csv");
    csv_file << "Instance,NN_Undelivered,NN_Cost,GA_Undelivered,GA_Cost,"
             << "Prep_ms,OSRM_ms,NN_ms,GA_ms\n";

    std::atomic<int> current_idx(0);
    int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    std::cout << "Starting " << num_threads << " parallel threads..." << std::endl;

    auto worker = [&]() {
        while (true) {
            int idx = current_idx.fetch_add(1);
            if (idx >= total) break;
            processInstance(json_files[idx], idx + 1, total, csv_file);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    csv_file.close();
    std::cout << "\nBenchmarking complete. Results saved to benchmark_results.csv" << std::endl;
    return 0;
}
