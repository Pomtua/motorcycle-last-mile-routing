#include <cmath>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

#include "router/cost.hpp"
#include "router/instance_io.hpp"
#include "router/local_search.hpp"
#include "router/solomon_i1.hpp"
#include "router/validate.hpp"
#include "router/zones.hpp"

namespace
{
    constexpr double kTolerance = 1e-6;
    constexpr int kNumZones = 3;
    constexpr double kZonePenalty = 100000.0;

    int failures = 0;

    void expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << "\n";
            ++failures;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: test_local_search <instance.json>\n";
        return 2;
    }

    try
    {
        const router::Instance inst =
            router::loadInstance(argv[1]);
        const router::Solution seed =
            router::solomonI1(inst, false);
        const router::Solution distanceOnly =
            router::localSearch(inst, seed);

        const std::vector<int> zoneOf =
            router::assignZones(inst, kNumZones);
        const router::Solution zeroPenalty =
            router::localSearch(inst, seed, zoneOf, 0.0);
        const router::Solution zoneAware =
            router::localSearch(
                inst, seed, zoneOf, kZonePenalty);

        const router::ValidationReport distanceReport =
            router::validate(inst, distanceOnly);
        const router::ValidationReport zeroPenaltyReport =
            router::validate(inst, zeroPenalty);
        const router::ValidationReport zoneAwareReport =
            router::validate(inst, zoneAware);

        expect(
            distanceReport.feasible,
            "distance-only local search must remain valid");
        expect(
            zeroPenaltyReport.feasible,
            "zero-penalty local search must remain valid");
        expect(
            zoneAwareReport.feasible,
            "zone-aware local search must remain valid");

        const double seedCost =
            router::computeCost(inst, seed);
        const double distanceCost =
            router::computeCost(inst, distanceOnly);
        const double zeroPenaltyCost =
            router::computeCost(inst, zeroPenalty);
        const double zoneAwareCost =
            router::computeCost(inst, zoneAware);

        expect(
            distanceCost <= seedCost + kTolerance,
            "distance-only local search must not worsen I1");
        expect(
            std::abs(zeroPenaltyCost - distanceCost) <
                kTolerance,
            "zero zone penalty must preserve distance-only cost");
        expect(
            zeroPenalty.routes.size() ==
                distanceOnly.routes.size(),
            "zero zone penalty must preserve route count");

        const router::ZoneMetrics distanceMetrics =
            router::measureZoneCoherence(
                distanceOnly, zoneOf);
        const router::ZoneMetrics zoneAwareMetrics =
            router::measureZoneCoherence(
                zoneAware, zoneOf);

        const double distanceScore =
            distanceCost +
            router::computeRouteZoneCost(
                distanceOnly, zoneOf, kZonePenalty);
        const double zoneAwareScore =
            zoneAwareCost +
            router::computeRouteZoneCost(
                zoneAware, zoneOf, kZonePenalty);

        expect(
            zoneAwareScore <
                distanceScore - kTolerance,
            "zone-aware search must improve combined score");
        expect(
            zoneAwareMetrics.routeZoneExcess <
                distanceMetrics.routeZoneExcess,
            "zone-aware search must reduce route-zone excess");

        if (failures != 0)
        {
            std::cerr << failures
                      << " local-search test(s) failed\n";
            return 1;
        }

        std::cout << "All local-search tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
