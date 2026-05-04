#include "KMeansClusterer.h"
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include <iostream>

namespace routing {

struct Centroid {
    double lat;
    double lon;
};

static double calculateDistance(double lat1, double lon1, double lat2, double lon2) {

    double dlat = lat1 - lat2;
    double dlon = lon1 - lon2;
    return std::sqrt(dlat * dlat + dlon * dlon);
}

void KMeansClusterer::process(RoutingInstance& instance) {
    if (instance.parcels.empty() || instance.vehicles.empty()) return;

    int k = instance.vehicles.size();
    if (k > instance.parcels.size()) {
        k = instance.parcels.size();
    }

    std::vector<Centroid> centroids(k);
    std::mt19937 rng(42); 
    std::uniform_int_distribution<int> dist(0, instance.parcels.size() - 1);

    for (int i = 0; i < k; ++i) {
        const auto& p = instance.parcels[dist(rng)];
        int loc_idx = instance.id_to_index.at(p.location_id);
        centroids[i] = {instance.locations[loc_idx].lat, instance.locations[loc_idx].lon};
    }

    std::vector<int> assignments(instance.parcels.size(), -1);
    bool changed = true;
    int iterations = 0;
    const int MAX_ITER = 100;

    while (changed && iterations < MAX_ITER) {
        changed = false;
        iterations++;

        for (size_t p_idx = 0; p_idx < instance.parcels.size(); ++p_idx) {
            const auto& p = instance.parcels[p_idx];
            int loc_idx = instance.id_to_index.at(p.location_id);
            double plat = instance.locations[loc_idx].lat;
            double plon = instance.locations[loc_idx].lon;

            int best_c = -1;
            double min_dist = std::numeric_limits<double>::max();

            for (int c = 0; c < k; ++c) {
                double d = calculateDistance(plat, plon, centroids[c].lat, centroids[c].lon);
                if (d < min_dist) {
                    min_dist = d;
                    best_c = c;
                }
            }

            if (assignments[p_idx] != best_c) {
                assignments[p_idx] = best_c;
                changed = true;
            }
        }

        std::vector<double> sum_lat(k, 0.0);
        std::vector<double> sum_lon(k, 0.0);
        std::vector<int> count(k, 0);

        for (size_t p_idx = 0; p_idx < instance.parcels.size(); ++p_idx) {
            int c = assignments[p_idx];
            int loc_idx = instance.id_to_index.at(instance.parcels[p_idx].location_id);
            sum_lat[c] += instance.locations[loc_idx].lat;
            sum_lon[c] += instance.locations[loc_idx].lon;
            count[c]++;
        }

        for (int c = 0; c < k; ++c) {
            if (count[c] > 0) {
                centroids[c].lat = sum_lat[c] / count[c];
                centroids[c].lon = sum_lon[c] / count[c];
            } else {

                const auto& p = instance.parcels[dist(rng)];
                int loc_idx = instance.id_to_index.at(p.location_id);
                centroids[c] = {instance.locations[loc_idx].lat, instance.locations[loc_idx].lon};
                changed = true;
            }
        }
    }

    for (size_t p_idx = 0; p_idx < instance.parcels.size(); ++p_idx) {
        int loc_idx = instance.id_to_index.at(instance.parcels[p_idx].location_id);
        instance.locations[loc_idx].zone_id = "ZONE_" + std::to_string(assignments[p_idx]);
    }
}

}
