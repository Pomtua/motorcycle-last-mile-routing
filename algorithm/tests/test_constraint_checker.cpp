#include <gtest/gtest.h>
#include "ConstraintChecker.h"
#include "RoutingInstance.h"

using namespace routing;

TEST(ConstraintCheckerTest, ValidRoute) {
    RoutingInstance instance;
    
    Vehicle v;
    v.id = 0;
    v.capacity_weight = 100.0;
    v.capacity_volume = 100.0;
    instance.vehicles.push_back(v);
    
    Parcel p;
    p.id = 1;
    p.location_id = 1;
    p.weight = 50.0;
    p.volume = 50.0;
    p.time_window = {0, 100};
    instance.parcels.push_back(p);
    
    Location depot; depot.id = 0; depot.zone_id = "DEPOT";
    Location loc1; loc1.id = 1; loc1.zone_id = "ZONE_UNK";
    instance.locations = {depot, loc1};

    instance.parcel_id_to_index.resize(2);
    instance.parcel_id_to_index[1] = 0;
    
    instance.id_to_index.resize(2);
    instance.id_to_index[0] = 0;
    instance.id_to_index[1] = 1;
    
    instance.duration_matrix = {{0, 10}, {10, 0}};
    instance.distance_matrix = {{0, 100}, {100, 0}};
    
    Route route;
    route.vehicle_id = 0;
    route.parcel_ids = {1};
    route.location_ids = {0, 1, 0};
    route.arrival_times = {0.0, 10.0, 20.0};
    
    EXPECT_TRUE(ConstraintChecker::evaluateRoute(instance, route, instance.vehicles[0]));
}

TEST(ConstraintCheckerTest, WeightViolation) {
    RoutingInstance instance;
    Vehicle v;
    v.id = 0;
    v.capacity_weight = 40.0; 
    v.capacity_volume = 100.0;
    instance.vehicles.push_back(v);
    
    Parcel p;
    p.id = 1;
    p.location_id = 1;
    p.weight = 50.0;
    p.volume = 50.0;
    p.time_window = {0, 100};
    instance.parcels.push_back(p);
    
    Location depot; depot.id = 0; depot.zone_id = "DEPOT";
    Location loc1; loc1.id = 1; loc1.zone_id = "ZONE_UNK";
    instance.locations = {depot, loc1};

    instance.parcel_id_to_index.resize(2);
    instance.parcel_id_to_index[1] = 0;
    
    instance.id_to_index.resize(2);
    instance.id_to_index[0] = 0;
    instance.id_to_index[1] = 1;
    instance.duration_matrix = {{0, 10}, {10, 0}};
    instance.distance_matrix = {{0, 100}, {100, 0}};
    
    Route route;
    route.vehicle_id = 0;
    route.parcel_ids = {1};
    route.location_ids = {0, 1, 0};
    route.arrival_times = {0.0, 10.0, 20.0};
    
    EXPECT_FALSE(ConstraintChecker::evaluateRoute(instance, route, instance.vehicles[0]));
}
