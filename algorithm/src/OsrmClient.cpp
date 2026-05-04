#include "OsrmClient.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace routing {
namespace fs = std::filesystem;

OsrmClient::OsrmClient(const std::string& host, int port) 
    : host_(host), port_(port) {}

void OsrmClient::fillMatrices(RoutingInstance& instance, const std::string& cache_key) {
    if (instance.locations.empty()) return;

    std::string cache_file = "";
    if (!cache_key.empty()) {
        fs::create_directories("osrm_cache");
        cache_file = "osrm_cache/" + cache_key + ".bin";
        if (fs::exists(cache_file)) {
            if (loadFromCache(instance, cache_file)) {
                return;
            }
        }
    }

    httplib::Client cli(host_, port_);
    cli.set_read_timeout(60); 

    std::string coords = formatCoordinates(instance.locations);
    std::string path = "/table/v1/motorcycle/" + coords + "?annotations=duration,distance";

    auto res = cli.Get(path.c_str());

    if (res && res->status == 200) {
        auto j = nlohmann::json::parse(res->body);

        if (j["code"] == "Ok") {
            int n = instance.locations.size();
            instance.duration_matrix.assign(n, std::vector<double>(n));
            instance.distance_matrix.assign(n, std::vector<double>(n));

            for (int i = 0; i < n; ++i) {
                for (int j_idx = 0; j_idx < n; ++j_idx) {
                    instance.duration_matrix[i][j_idx] = j["durations"][i][j_idx];
                    instance.distance_matrix[i][j_idx] = j["distances"][i][j_idx];
                }
            }

            if (!cache_file.empty()) {
                saveToCache(instance, cache_file);
            }

        } else {
            throw std::runtime_error("OSRM Error: " + j["code"].get<std::string>());
        }
    } else {
        std::string err_msg = res ? "Status: " + std::to_string(res->status) : "Connection failed";
        throw std::runtime_error("Failed to connect to OSRM: " + err_msg);
    }
}

std::string OsrmClient::formatCoordinates(const std::vector<Location>& locations) {
    std::stringstream ss;
    ss.precision(10);
    for (size_t i = 0; i < locations.size(); ++i) {
        ss << locations[i].lon << "," << locations[i].lat;
        if (i < locations.size() - 1) ss << ";";
    }
    return ss.str();
}

bool OsrmClient::loadFromCache(RoutingInstance& instance, const std::string& cache_file) {
    std::ifstream in(cache_file, std::ios::binary);
    if (!in) return false;

    int n;
    in.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (n != (int)instance.locations.size()) return false;

    instance.duration_matrix.assign(n, std::vector<double>(n));
    instance.distance_matrix.assign(n, std::vector<double>(n));

    for (int i = 0; i < n; ++i) {
        in.read(reinterpret_cast<char*>(instance.duration_matrix[i].data()), n * sizeof(double));
        in.read(reinterpret_cast<char*>(instance.distance_matrix[i].data()), n * sizeof(double));
    }
    return true;
}

void OsrmClient::saveToCache(const RoutingInstance& instance, const std::string& cache_file) {
    std::ofstream out(cache_file, std::ios::binary);
    if (!out) return;

    int n = instance.locations.size();
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));

    for (int i = 0; i < n; ++i) {
        out.write(reinterpret_cast<const char*>(instance.duration_matrix[i].data()), n * sizeof(double));
        out.write(reinterpret_cast<const char*>(instance.distance_matrix[i].data()), n * sizeof(double));
    }
}

}
