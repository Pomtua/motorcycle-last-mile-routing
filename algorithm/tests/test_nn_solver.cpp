#include <gtest/gtest.h>
#include "NearestNeighborSolver.h"
#include "RoutingInstance.h"

using namespace routing;

TEST(NearestNeighborTest, SolvesTrivialInstance) {
    RoutingInstance instance;
    
    Vehicle v;
    v.id = 0;
    v.capacity_weight = 100.0;
    v.capacity_volume = 100.0;
    instance.vehicles.push_back(v);
    
    Parcel p1; p1.id = 1; p1.location_id = 1; p1.weight = 10.0; p1.volume = 10.0; p1.time_window = {0, 1000};
    Parcel p2; p2.id = 2; p2.location_id = 2; p2.weight = 10.0; p2.volume = 10.0; p2.time_window = {0, 1000};
    instance.parcels = {p1, p2};
    
    Location depot; depot.id = 0; depot.zone_id = "DEPOT";
    Location loc1; loc1.id = 1; loc1.zone_id = "ZONE_UNK";
    Location loc2; loc2.id = 2; loc2.zone_id = "ZONE_UNK";
    instance.locations = {depot, loc1, loc2};

    instance.parcel_id_to_index.resize(3);
    instance.parcel_id_to_index[1] = 0;
    instance.parcel_id_to_index[2] = 1;
    
    instance.id_to_index.resize(3);
    instance.id_to_index[0] = 0;
    instance.id_to_index[1] = 1;
    instance.id_to_index[2] = 2;
    
    instance.distance_matrix = {
        {0, 10, 20},
        {10, 0, 10},
        {20, 10, 0}
    };
    instance.duration_matrix = {
        {0, 600, 1200},
        {600, 0, 600},
        {1200, 600, 0}
    };
    
    NearestNeighborSolver solver;
    RoutingResult result = solver.solve(instance);
    
    EXPECT_TRUE(result.all_parcels_delivered);
    EXPECT_EQ(result.undelivered_count, 0);
    EXPECT_EQ(result.routes.size(), 1);
    EXPECT_EQ(result.routes[0].parcel_ids.size(), 2);
    // Best route from 0 is to 1, then 2, then back to 0. Distance: 10 + 10 + 20 = 40.
    EXPECT_EQ(result.routes[0].route_distance, 40.0);
}
