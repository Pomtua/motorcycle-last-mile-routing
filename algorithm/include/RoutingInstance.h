#pragma once
#include <vector>
#include <map>
#include "Location.h"
#include "Parcel.h"
#include "Vehicle.h"

namespace routing {

struct RoutingInstance {
    std::vector<Location> locations;
    std::vector<Parcel> parcels;
    std::vector<Vehicle> vehicles;

    std::vector<std::vector<double>> distance_matrix;
    std::vector<std::vector<double>> duration_matrix;

    std::vector<int> id_to_index;        
    std::vector<int> parcel_id_to_index; 
};

}
