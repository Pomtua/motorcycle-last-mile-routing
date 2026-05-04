#include "SplitDeliveryProcessor.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace routing {

void SplitDeliveryProcessor::process(RoutingInstance& instance) {
    if (instance.vehicles.empty() || instance.parcels.empty()) return;

    double max_weight_cap = 0.0;
    double max_volume_cap = 0.0;

    for (const auto& v : instance.vehicles) {
        max_weight_cap = std::max(max_weight_cap, v.capacity_weight);
        max_volume_cap = std::max(max_volume_cap, v.capacity_volume);
    }

    if (max_weight_cap <= 0 || max_volume_cap <= 0) return;

    std::vector<Parcel> processed_parcels;
    int next_virtual_id = 100000; 

    for (const auto& parcel : instance.parcels) {
        if (parcel.weight > max_weight_cap || parcel.volume > max_volume_cap) {

            double remaining_weight = parcel.weight;
            double remaining_volume = parcel.volume;

            while (remaining_weight > 0.001 || remaining_volume > 0.001) {

                double weight_fraction = (remaining_weight > max_weight_cap) ? (max_weight_cap / remaining_weight) : 1.0;
                double volume_fraction = (remaining_volume > max_volume_cap) ? (max_volume_cap / remaining_volume) : 1.0;

                double fraction = std::min({1.0, weight_fraction, volume_fraction});

                double chunk_weight = remaining_weight * fraction;
                double chunk_volume = remaining_volume * fraction;

                if (chunk_weight < 0.001 && chunk_volume < 0.001) break;

                Parcel virtual_parcel = parcel;
                virtual_parcel.id = next_virtual_id++;
                virtual_parcel.weight = chunk_weight;
                virtual_parcel.volume = chunk_volume;
                virtual_parcel.is_split = true;
                processed_parcels.push_back(virtual_parcel);

                remaining_weight -= chunk_weight;
                remaining_volume -= chunk_volume;
            }
        } else {
            processed_parcels.push_back(parcel);
        }

    }

    instance.parcels = processed_parcels;

    int max_id = 0;
    for (const auto& p : instance.parcels) {
        if (p.id > max_id) max_id = p.id;
    }

    instance.parcel_id_to_index.assign(max_id + 1, -1);
    for (size_t i = 0; i < instance.parcels.size(); ++i) {
        instance.parcel_id_to_index[instance.parcels[i].id] = i;
    }
}

}
