#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <utility>

#include "router/instance.hpp"
#include "router/solution.hpp"
#include "router/zones.hpp"

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

    template <typename Function>
    void expectInvalidArgument(
        Function function,
        std::string_view message)
    {
        try
        {
            function();
            expect(false, message);
        }
        catch (const std::invalid_argument &)
        {
        }
        catch (...)
        {
            expect(false, message);
        }
    }

    router::Instance makeClusteredInstance()
    {
        router::Instance inst;
        inst.seed = 42;
        inst.n = 4;
        inst.nodes.resize(5);

        inst.nodes[1].lat = 13.000;
        inst.nodes[1].lng = 100.000;
        inst.nodes[2].lat = 13.001;
        inst.nodes[2].lng = 100.001;
        inst.nodes[3].lat = 14.000;
        inst.nodes[3].lng = 101.000;
        inst.nodes[4].lat = 14.001;
        inst.nodes[4].lng = 101.001;

        return inst;
    }

    router::Solution makeMeasuredSolution()
    {
        router::Route firstRoute;
        firstRoute.stops = {
            {1, 0, 1, 0.0, 0.0},
            {2, 0, 1, 0.0, 0.0},
            {3, 0, 1, 0.0, 0.0}};

        router::Route secondRoute;
        secondRoute.stops = {
            {4, 0, 1, 0.0, 0.0}};

        router::Solution solution;
        solution.routes = {
            std::move(firstRoute),
            std::move(secondRoute)};
        return solution;
    }
}

int main()
{
    const router::Instance inst = makeClusteredInstance();
    const std::vector<int> firstAssignment =
        router::assignZones(inst, 2);
    const std::vector<int> secondAssignment =
        router::assignZones(inst, 2);

    expect(
        firstAssignment == secondAssignment,
        "zone assignment must be deterministic");
    expect(
        firstAssignment.size() == 5,
        "zone assignment must include depot position");
    expect(
        firstAssignment[0] == -1,
        "depot must not belong to a customer zone");

    const std::set<int> usedZones(
        firstAssignment.begin() + 1,
        firstAssignment.end());
    expect(
        usedZones.size() == 2,
        "both requested zones must be used");
    expect(
        firstAssignment[1] == firstAssignment[2],
        "nearby customers 1 and 2 must share a zone");
    expect(
        firstAssignment[3] == firstAssignment[4],
        "nearby customers 3 and 4 must share a zone");
    expect(
        firstAssignment[1] != firstAssignment[3],
        "separated customer groups must use different zones");

    const router::Solution solution = makeMeasuredSolution();
    const std::vector<int> zoneOf = {-1, 0, 0, 1, 1};
    const router::ZoneMetrics metrics =
        router::measureZoneCoherence(solution, zoneOf);

    expect(
        metrics.fragmentation == 1,
        "fragmentation must count the extra route serving zone 1");
    expect(
        metrics.routeZoneExcess == 1,
        "route-zone excess must count one extra zone");
    expect(
        std::abs(metrics.averageZonesPerRoute - 1.5) < 1e-12,
        "average zones per route must equal 1.5");
    expect(
        std::abs(
            router::computeRouteZoneCost(
                solution, zoneOf, 250.0) -
            250.0) < 1e-12,
        "route-zone cost must equal excess times penalty");

    expectInvalidArgument(
        [&]()
        {
            router::computeRouteZoneCost(
                solution, zoneOf, -1.0);
        },
        "negative zone penalty must be rejected");

    expectInvalidArgument(
        [&]()
        {
            router::measureZoneCoherence(
                solution, {-1, 0});
        },
        "missing customer zone assignment must be rejected");

    if (failures != 0)
    {
        std::cerr << failures << " zone test(s) failed\n";
        return 1;
    }

    std::cout << "All zone tests passed\n";
    return 0;
}
