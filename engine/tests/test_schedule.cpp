#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

#include "router/instance_io.hpp"
#include "router/schedule.hpp"

namespace
{
    int failures = 0;

    void expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << "\n";
            ++failures;
        }
    }

    bool near(double a, double b)
    {
        return std::abs(a - b) < 1e-9;
    }

    nlohmann::json instanceJson()
    {
        return {
            {"meta", {{"seed", 7}, {"n", 2}, {"horizon", 1000.0}}},
            {"fleet", {{"size", 2}, {"weight_capacity", 10.0}, {"volume_capacity", 1.0}}},
            {"nodes", nlohmann::json::array({
                          {{"osm_id", "depot"}, {"type", "depot"}, {"lng", 100.0}, {"lat", 13.0}, {"demand_weight", 0.0},
                           {"demand_volume", 0.0}, {"tw_start", 0}, {"tw_end", 1000}, {"service_time", 0}},
                          {{"osm_id", "A"}, {"type", "customer"}, {"lng", 100.1}, {"lat", 13.1}, {"demand_weight", 4.0},
                           {"demand_volume", 0.4}, {"tw_start", 100}, {"tw_end", 200}, {"service_time", 30}},
                          {{"osm_id", "B"}, {"type", "customer"}, {"lng", 100.2}, {"lat", 13.2}, {"demand_weight", 2.0},
                           {"demand_volume", 0.2}, {"tw_start", 0}, {"tw_end", 150}, {"service_time", 10}},
                      })},
            {"duration_matrix", {{0, 50, 60}, {50, 0, 40}, {60, 40, 0}}},
            {"distance_matrix", {{0, 500, 600}, {500, 0, 400}, {600, 400, 0}}}};
    }

    void testInstanceFromJson()
    {
        const router::Instance inst = router::instanceFromJson(instanceJson());
        expect(inst.n == 2 && inst.nodes.size() == 3, "instanceFromJson reads every node");
        expect(inst.seed == 7, "instanceFromJson reads the seed");
        expect(near(inst.fleet.weightCapacity, 10.0), "instanceFromJson reads the fleet");
        expect(near(inst.distanceMatrix[1][2], 400.0), "instanceFromJson reads the distance matrix");

        nlohmann::json ragged = instanceJson();
        ragged["duration_matrix"][1] = {50, 0};
        try
        {
            router::instanceFromJson(ragged);
            expect(false, "instanceFromJson rejects a ragged matrix row");
        }
        catch (const std::runtime_error &)
        {
        }

        nlohmann::json shortNodes = instanceJson();
        shortNodes["meta"]["n"] = 3;
        try
        {
            router::instanceFromJson(shortNodes);
            expect(false, "instanceFromJson rejects a node count that does not match meta.n");
        }
        catch (const std::runtime_error &)
        {
        }
    }

    void testScheduleWaitsAndReturns()
    {
        const router::Instance inst = router::instanceFromJson(instanceJson());
        router::Route route;
        route.stops.push_back({1, 0, 1, 4.0, 0.4});
        route.stops.push_back({2, 0, 1, 2.0, 0.2});

        const router::RouteSchedule schedule = router::scheduleRoute(inst, route);
        expect(schedule.stops.size() == 2, "one timing per stop");
        expect(near(schedule.stops[0].arrival, 50.0), "first arrival is the depot travel time");
        expect(near(schedule.stops[0].start, 100.0), "service waits for the window to open");
        expect(near(schedule.stops[0].departure, 130.0), "departure adds the service time");
        expect(schedule.stops[0].onTime, "first stop is on time");
        expect(near(schedule.stops[1].arrival, 170.0), "second arrival follows the first departure");
        expect(!schedule.stops[1].onTime, "arriving after the window end is late");
        expect(near(schedule.returnTime, 240.0), "return time adds the trip back to the depot");
        expect(near(schedule.distance, 1500.0), "distance includes the return leg");
        expect(near(schedule.loadWeight, 6.0) && near(schedule.loadVolume, 0.6), "load sums the deliveries");
        expect(schedule.withinHorizon, "route ends inside the horizon");
    }

    void testScheduleHorizon()
    {
        nlohmann::json json = instanceJson();
        json["meta"]["horizon"] = 100.0;
        const router::Instance inst = router::instanceFromJson(json);
        router::Route route;
        route.stops.push_back({2, 0, 1, 2.0, 0.2});
        const router::RouteSchedule schedule = router::scheduleRoute(inst, route);
        expect(near(schedule.returnTime, 130.0), "return time for a single stop");
        expect(!schedule.withinHorizon, "a route ending after the horizon is flagged");
    }
}

int main()
{
    testInstanceFromJson();
    testScheduleWaitsAndReturns();
    testScheduleHorizon();
    if (failures > 0)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "schedule tests passed\n";
    return 0;
}
