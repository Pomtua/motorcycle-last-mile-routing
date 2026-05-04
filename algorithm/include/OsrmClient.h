#pragma once
#include <string>
#include <vector>
#include "RoutingInstance.h"

namespace routing {

class OsrmClient {
public:
    OsrmClient(const std::string& host = "localhost", int port = 5000);

    void fillMatrices(RoutingInstance& instance, const std::string& cache_key = "");

private:
    std::string host_;
    int port_;

    std::string formatCoordinates(const std::vector<Location>& locations);

    bool loadFromCache(RoutingInstance& instance, const std::string& cache_file);
    void saveToCache(const RoutingInstance& instance, const std::string& cache_file);
};

}
