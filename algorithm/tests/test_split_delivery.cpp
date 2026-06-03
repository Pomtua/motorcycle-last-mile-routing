#include <gtest/gtest.h>
#include "SplitDeliveryProcessor.h"
#include "RoutingInstance.h"

using namespace routing;

TEST(SplitDeliveryTest, SplitsOversizedParcels) {
    RoutingInstance instance;
    
    Vehicle v1; v1.capacity_weight = 100.0; v1.capacity_volume = 250.0;
    Vehicle v2; v2.capacity_weight = 100.0; v2.capacity_volume = 250.0;
    instance.vehicles = {v1, v2};
    
    Parcel p;
    p.id = 1;
    p.weight = 150.0; // Needs split
    p.volume = 300.0; // Needs split
    instance.parcels.push_back(p);
    
    SplitDeliveryProcessor::process(instance);
    
    EXPECT_GT(instance.parcels.size(), 1);
    
    double total_w = 0;
    double total_v = 0;
    for (const auto& sub : instance.parcels) {
        total_w += sub.weight;
        total_v += sub.volume;
        EXPECT_LE(sub.weight, 100.0);
        EXPECT_LE(sub.volume, 250.0);
    }
    
    EXPECT_NEAR(total_w, 150.0, 0.001);
    EXPECT_NEAR(total_v, 300.0, 0.001);
}
