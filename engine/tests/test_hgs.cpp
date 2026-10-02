#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "router/hgs.hpp"

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

    template <typename Fn>
    void expectInvalidArgument(Fn &&fn, std::string_view message)
    {
        try
        {
            fn();
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

    router::Instance makeSegmentInstance()
    {
        router::Instance inst;
        inst.n = 2;
        inst.horizon = 30.0;
        inst.fleet.size = 2;
        inst.fleet.weightCapacity = 10.0;
        inst.fleet.volumeCapacity = 1.0;
        inst.nodes.resize(3);

        inst.nodes[1].serviceTime = 5;
        inst.nodes[1].twStart = 0;
        inst.nodes[1].twEnd = 20;

        inst.nodes[2].serviceTime = 5;
        inst.nodes[2].twStart = 0;
        inst.nodes[2].twEnd = 20;

        inst.distanceMatrix = {
            {0.0, 10.0, 10.0},
            {10.0, 0.0, 10.0},
            {10.0, 10.0, 0.0}};
        inst.durationMatrix = inst.distanceMatrix;

        return inst;
    }
}

int main()
{
    const std::vector<router::HgsGene> canonical =
        router::makeCanonicalGiantTour(3);
    const std::vector<router::HgsGene> expectedCanonical = {
        {0},
        {1},
        {2}};

    expect(
        canonical == expectedCanonical,
        "canonical tour must follow visit-catalog order");
    expect(
        router::isCompleteGiantTourPermutation(
            canonical, 3),
        "canonical tour must be a complete permutation");

    const std::vector<router::HgsGene> reordered = {
        {2},
        {0},
        {1}};
    expect(
        router::isCompleteGiantTourPermutation(
            reordered, 3),
        "reordered complete tour must be accepted");

    const std::vector<router::HgsGene> missing = {
        {0},
        {1}};
    expect(
        !router::isCompleteGiantTourPermutation(
            missing, 3),
        "tour with a missing visit must be rejected");

    const std::vector<router::HgsGene> duplicate = {
        {0},
        {1},
        {1}};
    expect(
        !router::isCompleteGiantTourPermutation(
            duplicate, 3),
        "tour with a duplicate visit must be rejected");

    const std::vector<router::HgsGene> outOfRange = {
        {0},
        {1},
        {3}};
    expect(
        !router::isCompleteGiantTourPermutation(
            outOfRange, 3),
        "tour with an out-of-range visit must be rejected");

    expect(
        router::isCompleteGiantTourPermutation({}, 0),
        "empty tour must be valid for an empty catalogue");

    const router::Instance segmentInstance =
        makeSegmentInstance();
    const std::vector<router::Visit> segmentCatalog = {
        {1, 0, 2, 6.0, 0.4},
        {1, 1, 2, 6.0, 0.4},
        {2, 0, 1, 5.0, 0.7}};
    const std::vector<router::HgsGene> segmentTour = {
        {0},
        {2},
        {1}};

    const router::HgsRouteSegmentEvaluation segment =
        router::evaluateGiantTourSegment(
            segmentInstance,
            segmentCatalog,
            segmentTour,
            0,
            2);

    expect(
        segment.route.stops.size() == 2 &&
            segment.route.stops[0].chunkIdx == 0 &&
            segment.route.stops[1].nodeIndex == 2,
        "segment decoding must preserve giant-tour order");
    expect(
        std::abs(segment.distanceCost - 30.0) < 1e-12,
        "segment distance must include both depot legs");
    expect(
        std::abs(
            segment.violations.weightExcess -
            1.0) < 1e-12,
        "segment must measure weight excess");
    expect(
        std::abs(
            segment.violations.volumeExcess -
            0.1) < 1e-12,
        "segment must measure volume excess");
    expect(
        std::abs(
            segment.violations.timeWarp -
            10.0) < 1e-12,
        "segment must measure time-window and horizon warp");
    expect(
        !segment.hasCustomerConflict,
        "different customers must not create a conflict");
    expect(
        segment.routeZoneExcess == 0,
        "unzoned segment must have zero zone excess");

    const std::vector<int> zoneOf = {
        -1,
        0,
        1};
    const router::HgsRouteSegmentEvaluation zonedSegment =
        router::evaluateGiantTourSegment(
            segmentInstance,
            segmentCatalog,
            segmentTour,
            0,
            2,
            zoneOf);

    expect(
        zonedSegment.routeZoneExcess == 1,
        "route visiting two zones must have one excess zone");

    const router::HgsRouteSegmentEvaluation conflictingSegment =
        router::evaluateGiantTourSegment(
            segmentInstance,
            segmentCatalog,
            segmentTour,
            0,
            3);

    expect(
        conflictingSegment.hasCustomerConflict,
        "two chunks of one customer on a route must conflict");

    expectInvalidArgument(
        [&]()
        {
            router::evaluateGiantTourSegment(
                segmentInstance,
                segmentCatalog,
                segmentTour,
                1,
                1);
        },
        "empty route segment must be rejected");

    expectInvalidArgument(
        [&]()
        {
            router::evaluateGiantTourSegment(
                segmentInstance,
                segmentCatalog,
                {{3}},
                0,
                1);
        },
        "out-of-range gene must be rejected");

    if (failures != 0)
    {
        std::cerr << failures << " HGS test(s) failed\n";
        return 1;
    }

    std::cout << "All HGS tests passed\n";
    return 0;
}
