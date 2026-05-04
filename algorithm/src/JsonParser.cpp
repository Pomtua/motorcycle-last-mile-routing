#include "JsonParser.h"
#include <fstream>
#include <iostream>

namespace routing {

RoutingInstance JsonParser::parse(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + file_path);
    }

    nlohmann::json j;
    file >> j;

    RoutingInstance instance;

    auto resources = j["resources"];

    Location depot;
    depot.id = 0;
    depot.lat = resources["depot_location"]["lat"];
    depot.lon = resources["depot_location"]["lng"];
    depot.name = "Depot";
    depot.zone_id = "DEPOT";
    instance.locations.push_back(depot);

    instance.id_to_index.assign(10000, -1);
    instance.parcel_id_to_index.assign(10000, -1);

    instance.id_to_index[depot.id] = 0;

    int num_drivers = resources["num_drivers"];
    double max_weight = resources["max_weight_capacity"];
    double max_volume = resources["max_volume_capacity"];

    for (int i = 0; i < num_drivers; ++i) {
        Vehicle v;
        v.id = i;
        v.capacity_weight = max_weight;
        v.capacity_volume = max_volume;
        instance.vehicles.push_back(v);
    }

    int loc_idx = 1;
    for (auto& p_json : j["parcels"]) {

        Location loc;
        loc.id = p_json["id"];
        loc.lat = p_json["lat"];
        loc.lon = p_json["lng"];
        loc.name = "Customer_" + std::to_string(p_json["id"].get<int>());
        loc.zone_id = "ZONE_UNK"; 

        if (loc.id >= (int)instance.id_to_index.size()) {
            instance.id_to_index.resize(loc.id + 1000, -1);
        }
        instance.id_to_index[loc.id] = loc_idx++;
        instance.locations.push_back(loc);

        Parcel p;
        p.id = p_json["id"];
        p.location_id = loc.id;
        p.weight = p_json["weight"];
        p.volume = p_json["volume"];
        p.time_window.start = p_json["time_window"][0];
        p.time_window.end = p_json["time_window"][1];
        p.is_split = p_json.value("is_split_test", false);

        if (p.id >= (int)instance.parcel_id_to_index.size()) {
            instance.parcel_id_to_index.resize(p.id + 1000, -1);
        }
        instance.parcel_id_to_index[p.id] = (int)instance.parcels.size();
        instance.parcels.push_back(p);
    }

    return instance;
}

}
