#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "router/hgs.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"

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

    template <typename Function>
    void expectRuntimeError(
        Function function,
        std::string_view message)
    {
        try
        {
            function();
            expect(false, message);
        }
        catch (const std::runtime_error &)
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

    const router::HgsPenaltyWeights penaltyWeights = {
        2.0,
        100.0,
        3.0};
    const router::HgsIndividualEvaluation
        infeasibleEvaluation =
            router::aggregateHgsIndividualEvaluation(
                {zonedSegment},
                penaltyWeights,
                50.0);

    expect(
        std::abs(
            infeasibleEvaluation.distanceCost -
            30.0) < 1e-12,
        "individual distance must aggregate route distance");
    expect(
        infeasibleEvaluation.routeZoneExcess == 1,
        "individual must aggregate route-zone excess");
    expect(
        std::abs(
            infeasibleEvaluation.routeZoneCost -
            50.0) < 1e-12,
        "route-zone cost must apply the zone penalty");
    expect(
        std::abs(
            infeasibleEvaluation.objectiveCost -
            80.0) < 1e-12,
        "objective must combine distance and zone cost");
    expect(
        std::abs(
            infeasibleEvaluation.penalizedCost -
            122.0) < 1e-9,
        "penalized cost must include all violations");
    expect(
        !infeasibleEvaluation.feasible,
        "capacity and time violations must be infeasible");

    const router::HgsRouteSegmentEvaluation
        feasibleSegment =
            router::evaluateGiantTourSegment(
                segmentInstance,
                segmentCatalog,
                segmentTour,
                0,
                1);
    const router::HgsIndividualEvaluation
        feasibleEvaluation =
            router::aggregateHgsIndividualEvaluation(
                {feasibleSegment},
                penaltyWeights,
                50.0);

    expect(
        feasibleEvaluation.feasible,
        "violation-free individual must be feasible");
    expect(
        std::abs(
            feasibleEvaluation.penalizedCost -
            20.0) < 1e-12,
        "feasible individual must have no violation penalty");

    expectInvalidArgument(
        [&]()
        {
            router::aggregateHgsIndividualEvaluation(
                {conflictingSegment},
                penaltyWeights,
                50.0);
        },
        "hard customer conflict must not be aggregated");

    expectInvalidArgument(
        [&]()
        {
            router::aggregateHgsIndividualEvaluation(
                {feasibleSegment},
                {-1.0, 0.0, 0.0},
                50.0);
        },
        "negative adaptive penalty must be rejected");

    const router::HgsIndividual twoRouteIndividual =
        router::decodeGiantTour(
            segmentInstance,
            segmentCatalog,
            segmentTour,
            penaltyWeights);

    expect(
        twoRouteIndividual.decodedSolution.routes.size() == 2,
        "Split must respect the two-vehicle fleet limit");
    expect(
        twoRouteIndividual.giantTour == segmentTour,
        "decoded individual must retain its giant tour");
    expect(
        !twoRouteIndividual.evaluation.feasible,
        "two-route partition must retain measured violations");

    router::Instance threeVehicleInstance =
        segmentInstance;
    threeVehicleInstance.fleet.size = 3;

    const router::HgsIndividual threeRouteIndividual =
        router::decodeGiantTour(
            threeVehicleInstance,
            segmentCatalog,
            segmentTour,
            penaltyWeights);

    expect(
        threeRouteIndividual.decodedSolution.routes.size() ==
            3,
        "Split must use three routes when penalties justify them");
    expect(
        threeRouteIndividual.evaluation.feasible,
        "three singleton routes must be feasible");
    expect(
        std::abs(
            threeRouteIndividual.evaluation.penalizedCost -
            60.0) < 1e-12,
        "three singleton routes must cost 60");

    const router::HgsIndividual zonedIndividual =
        router::decodeGiantTour(
            segmentInstance,
            segmentCatalog,
            segmentTour,
            penaltyWeights,
            zoneOf,
            50.0);

    expect(
        zonedIndividual.decodedSolution.routes.size() == 2,
        "zoned Split must respect fleet size");
    expect(
        zonedIndividual.evaluation.routeZoneExcess == 1,
        "zoned Split must retain route-zone excess");

    expectInvalidArgument(
        [&]()
        {
            router::decodeGiantTour(
                segmentInstance,
                segmentCatalog,
                {{0}, {1}},
                penaltyWeights);
        },
        "incomplete giant tour must be rejected");

    router::Instance noFleetInstance =
        segmentInstance;
    noFleetInstance.fleet.size = 0;
    expectInvalidArgument(
        [&]()
        {
            router::decodeGiantTour(
                noFleetInstance,
                segmentCatalog,
                segmentTour,
                penaltyWeights);
        },
        "non-empty tour with no vehicles must be rejected");

    router::Instance oneVehicleInstance =
        segmentInstance;
    oneVehicleInstance.fleet.size = 1;
    expectRuntimeError(
        [&]()
        {
            router::decodeGiantTour(
                oneVehicleInstance,
                {segmentCatalog[0], segmentCatalog[1]},
                {{0}, {1}},
                penaltyWeights);
        },
        "same-customer chunks requiring two routes must fail");

    const router::HgsPenaltyWeights zeroPenalties = {};
    const router::HgsIndividual preservedSeed =
        router::makeHgsIndividualFromSolution(
            threeVehicleInstance,
            segmentCatalog,
            threeRouteIndividual.decodedSolution,
            zeroPenalties);

    expect(
        preservedSeed.decodedSolution.routes.size() == 3 &&
            preservedSeed.decodedSolution.routes[0].stops.size() == 1 &&
            preservedSeed.decodedSolution.routes[1].stops.size() == 1 &&
            preservedSeed.decodedSolution.routes[2].stops.size() == 1,
        "seed encoding must preserve its three route boundaries");
    expect(
        preservedSeed.giantTour == segmentTour,
        "seed encoding must retain visit and chunk order");
    expect(
        preservedSeed.evaluation.feasible &&
            std::abs(preservedSeed.evaluation.penalizedCost - 60.0) <
                1e-12,
        "feasible seed must remain feasible with zero penalties");

    const router::HgsIndividual repartitionedSeed =
        router::decodeGiantTour(
            threeVehicleInstance,
            segmentCatalog,
            preservedSeed.giantTour,
            zeroPenalties);
    expect(
        repartitionedSeed.decodedSolution.routes.size() == 2 &&
            !repartitionedSeed.evaluation.feasible,
        "Split may replace the seed partition with an infeasible one");

    const router::HgsIndividual preservedZoned =
        router::makeHgsIndividualFromSolution(
            segmentInstance,
            segmentCatalog,
            zonedIndividual.decodedSolution,
            penaltyWeights,
            zoneOf,
            50.0);
    expect(
        !preservedZoned.evaluation.feasible &&
            preservedZoned.evaluation.routeZoneExcess == 1 &&
            std::abs(
                preservedZoned.evaluation.penalizedCost -
                zonedIndividual.evaluation.penalizedCost) < 1e-9,
        "zoned solution encoding must retain its violations and score");

    expectInvalidArgument(
        [&]()
        {
            router::makeHgsIndividualFromSolution(
                segmentInstance,
                segmentCatalog,
                threeRouteIndividual.decodedSolution,
                zeroPenalties);
        },
        "solution encoding must reject a fleet overflow");

    router::Solution missingVisitSeed =
        threeRouteIndividual.decodedSolution;
    missingVisitSeed.routes.pop_back();
    expectInvalidArgument(
        [&]()
        {
            router::makeHgsIndividualFromSolution(
                threeVehicleInstance, segmentCatalog,
                missingVisitSeed, zeroPenalties);
        },
        "solution encoding must reject a missing chunk");

    router::Solution conflictingSeed;
    conflictingSeed.routes.push_back({segmentCatalog});
    expectInvalidArgument(
        [&]()
        {
            router::makeHgsIndividualFromSolution(
                threeVehicleInstance, segmentCatalog,
                conflictingSeed, zeroPenalties);
        },
        "solution encoding must reject same-customer route conflicts");

    router::Instance fourVehicleInstance = threeVehicleInstance;
    fourVehicleInstance.fleet.size = 4;
    router::Solution emptyRouteSeed =
        threeRouteIndividual.decodedSolution;
    emptyRouteSeed.routes.push_back({});
    expectInvalidArgument(
        [&]()
        {
            router::makeHgsIndividualFromSolution(
                fourVehicleInstance, segmentCatalog,
                emptyRouteSeed, zeroPenalties);
        },
        "solution encoding must reject an empty route");

    router::Instance populationInstance = threeVehicleInstance;
    populationInstance.fleet.weightCapacity = 20.0;
    populationInstance.fleet.volumeCapacity = 2.0;
    populationInstance.horizon = 100.0;
    populationInstance.distanceMatrix[1][2] = 5.0;
    populationInstance.distanceMatrix[2][1] = 1.0;
    populationInstance.durationMatrix[1][2] = 1.0;
    populationInstance.durationMatrix[2][1] = 30.0;

    const router::HgsIndividual populationSeed =
        router::makeHgsIndividualFromSolution(
            populationInstance, segmentCatalog,
            threeRouteIndividual.decodedSolution, zeroPenalties);

    router::Solution betterPopulationSolution;
    betterPopulationSolution.routes = {
        {{segmentCatalog[0], segmentCatalog[2]}},
        {{segmentCatalog[1]}}};
    const router::HgsIndividual betterPopulationIndividual =
        router::makeHgsIndividualFromSolution(
            populationInstance, segmentCatalog,
            betterPopulationSolution, zeroPenalties);

    router::Solution infeasiblePopulationSolution;
    infeasiblePopulationSolution.routes = {
        {{segmentCatalog[2], segmentCatalog[0]}},
        {{segmentCatalog[1]}}};
    const router::HgsIndividual infeasiblePopulationIndividual =
        router::makeHgsIndividualFromSolution(
            populationInstance, segmentCatalog,
            infeasiblePopulationSolution, zeroPenalties);

    router::HgsPopulation population(segmentCatalog.size());
    expect(
        population.feasibleIndividuals().empty() &&
            population.infeasibleIndividuals().empty() &&
            !population.bestFeasible().has_value(),
        "empty population must have no feasible incumbent");

    expect(
        !population.addIndividual(infeasiblePopulationIndividual) &&
            !population.bestFeasible().has_value(),
        "infeasible insertion must not create a feasible incumbent");
    expect(
        population.addIndividual(populationSeed),
        "first feasible individual must establish the incumbent");
    expect(
        !population.addIndividual(infeasiblePopulationIndividual) &&
            population.bestFeasible().has_value() &&
            std::abs(
                population.bestFeasible()->evaluation.objectiveCost -
                60.0) < 1e-12,
        "cheaper infeasible score must not replace the feasible seed");

    expect(
        population.addIndividual(betterPopulationIndividual) &&
            population.bestFeasible().has_value() &&
            std::abs(
                population.bestFeasible()->evaluation.objectiveCost -
                45.0) < 1e-12,
        "better feasible objective must replace the incumbent");
    expect(
        !population.addIndividual(populationSeed) &&
            population.bestFeasible().has_value() &&
            std::abs(
                population.bestFeasible()->evaluation.objectiveCost -
                45.0) < 1e-12,
        "worse feasible objective must retain the incumbent");

    router::Solution tiedPopulationSolution;
    tiedPopulationSolution.routes = {
        {{segmentCatalog[1], segmentCatalog[2]}},
        {{segmentCatalog[0]}}};
    const router::HgsIndividual tiedPopulationIndividual =
        router::makeHgsIndividualFromSolution(
            populationInstance, segmentCatalog,
            tiedPopulationSolution, zeroPenalties);
    expect(
        !population.addIndividual(tiedPopulationIndividual) &&
            population.bestFeasible().has_value() &&
            population.bestFeasible()->giantTour ==
                betterPopulationIndividual.giantTour,
        "equal feasible objectives must retain the existing incumbent");

    const router::HgsIndividual zonedPopulationSeed =
        router::makeHgsIndividualFromSolution(
            populationInstance, segmentCatalog,
            threeRouteIndividual.decodedSolution, zeroPenalties,
            zoneOf, 20.0);
    const router::HgsIndividual higherZoneCostIndividual =
        router::makeHgsIndividualFromSolution(
            populationInstance, segmentCatalog,
            betterPopulationSolution, zeroPenalties,
            zoneOf, 20.0);
    router::HgsPopulation objectivePopulation(segmentCatalog.size());
    expect(
        objectivePopulation.addIndividual(zonedPopulationSeed) &&
            !objectivePopulation.addIndividual(higherZoneCostIndividual) &&
            objectivePopulation.bestFeasible().has_value() &&
            std::abs(
                objectivePopulation.bestFeasible()->evaluation.objectiveCost -
                60.0) < 1e-12,
        "shorter distance with higher zone cost must not replace the incumbent");

    router::HgsIndividual incompleteIndividual =
        betterPopulationIndividual;
    incompleteIndividual.giantTour.pop_back();
    expectInvalidArgument(
        [&]() { population.addIndividual(incompleteIndividual); },
        "population must reject incomplete giant tours");

    router::HgsIndividual nonFiniteIndividual =
        betterPopulationIndividual;
    nonFiniteIndividual.evaluation.objectiveCost =
        std::numeric_limits<double>::quiet_NaN();
    expectInvalidArgument(
        [&]() { population.addIndividual(nonFiniteIndividual); },
        "population must reject non-finite scores");

    router::HgsIndividual inconsistentIndividual =
        infeasiblePopulationIndividual;
    inconsistentIndividual.evaluation.feasible = true;
    expectInvalidArgument(
        [&]() { population.addIndividual(inconsistentIndividual); },
        "population must reject contradictory feasibility flags");

    expect(
        population.feasibleIndividuals().size() == 4 &&
            population.infeasibleIndividuals().size() == 2,
        "population must separate feasible and infeasible members");
    expect(
        population.bestFeasible().has_value() &&
            population.bestFeasible()->decodedSolution.routes.size() == 2,
        "feasible incumbent must retain its decoded solution");

    const double boundaryDistance =
        router::directedBrokenPairsDistance(
            segmentCatalog,
            populationSeed.decodedSolution,
            betterPopulationIndividual.decodedSolution);
    expect(
        populationSeed.giantTour ==
                betterPopulationIndividual.giantTour &&
            std::abs(boundaryDistance - 1.0 / 3.0) < 1e-12,
        "diversity must detect route boundaries in identical giant tours");

    router::Solution reorderedRoutes = betterPopulationSolution;
    std::reverse(
        reorderedRoutes.routes.begin(), reorderedRoutes.routes.end());
    expect(
        router::directedBrokenPairsDistance(
            segmentCatalog, betterPopulationSolution,
            reorderedRoutes) == 0.0,
        "route ordering must not change broken-pairs distance");

    const double reversedRouteDistance =
        router::directedBrokenPairsDistance(
            segmentCatalog, betterPopulationSolution,
            infeasiblePopulationSolution);
    expect(
        std::abs(reversedRouteDistance - 2.0 / 3.0) < 1e-12,
        "reversing a two-visit route must change directed links");
    expect(
        std::abs(
            reversedRouteDistance -
            router::directedBrokenPairsDistance(
                segmentCatalog, infeasiblePopulationSolution,
                betterPopulationSolution)) < 1e-12,
        "broken-pairs distance must be symmetric between solutions");
    expect(
        router::directedBrokenPairsDistance(
            segmentCatalog, betterPopulationSolution,
            betterPopulationSolution) == 0.0,
        "identical solutions must have zero broken-pairs distance");

    expect(
        std::abs(
            router::directedBrokenPairsDistance(
                segmentCatalog, betterPopulationSolution,
                tiedPopulationSolution) - 0.5) < 1e-12,
        "diversity must distinguish chunks of the same customer");

    const std::vector<router::Visit> distinctVisitCatalog = {
        {1, 0, 1, 1.0, 0.1},
        {2, 0, 1, 1.0, 0.1},
        {3, 0, 1, 1.0, 0.1}};
    router::Solution forwardRoute;
    forwardRoute.routes.push_back({distinctVisitCatalog});
    router::Solution backwardRoute = forwardRoute;
    std::reverse(
        backwardRoute.routes[0].stops.begin(),
        backwardRoute.routes[0].stops.end());
    expect(
        router::directedBrokenPairsDistance(
            distinctVisitCatalog, forwardRoute, backwardRoute) == 1.0,
        "fully reversed directed links must reach distance one");

    expect(
        router::directedBrokenPairsDistance({}, {}, {}) == 0.0,
        "two empty solutions must have zero broken-pairs distance");

    expectInvalidArgument(
        [&]()
        {
            router::directedBrokenPairsDistance(
                segmentCatalog, missingVisitSeed,
                populationSeed.decodedSolution);
        },
        "broken-pairs distance must reject missing visits");

    router::Solution duplicateVisitSolution = betterPopulationSolution;
    duplicateVisitSolution.routes[0].stops[0] = segmentCatalog[2];
    expectInvalidArgument(
        [&]()
        {
            router::directedBrokenPairsDistance(
                segmentCatalog, betterPopulationSolution,
                duplicateVisitSolution);
        },
        "broken-pairs distance must reject duplicate visits");
    expectInvalidArgument(
        [&]()
        {
            router::directedBrokenPairsDistance(
                segmentCatalog, emptyRouteSeed,
                populationSeed.decodedSolution);
        },
        "broken-pairs distance must reject empty routes");

    const std::vector<router::HgsIndividual> fitnessIndividuals = {
        populationSeed, betterPopulationIndividual, tiedPopulationIndividual};
    const std::vector<router::HgsFitness> nearestFitness =
        router::computeHgsFitness(
            segmentCatalog, fitnessIndividuals, 1, 0);
    expect(
        nearestFitness.size() == 3 &&
            std::abs(nearestFitness[0].diversityContribution - 1.0 / 3.0) <
                1e-12 &&
            std::abs(nearestFitness[1].diversityContribution - 1.0 / 3.0) <
                1e-12,
        "diversity must average closest neighbours without including self");
    expect(
        nearestFitness[0].costRank == 1.0 &&
            nearestFitness[1].costRank == 0.5 &&
            nearestFitness[2].costRank == 0.5 &&
            std::abs(nearestFitness[0].diversityRank - 2.0 / 3.0) < 1e-12,
        "fitness must normalize ranks and assign average ranks to ties");
    expect(
        std::abs(nearestFitness[0].biasedFitness - 5.0 / 3.0) < 1e-12 &&
            std::abs(nearestFitness[1].biasedFitness - 7.0 / 6.0) < 1e-12,
        "biased fitness must combine cost and diversity ranks");

    const std::vector<router::HgsFitness> allNeighbourFitness =
        router::computeHgsFitness(
            segmentCatalog, fitnessIndividuals, 10, 0);
    expect(
        std::abs(allNeighbourFitness[1].diversityContribution - 5.0 / 12.0) <
                1e-12 &&
            allNeighbourFitness[1].diversityRank == 0.5 &&
            allNeighbourFitness[0].diversityRank == 1.0,
        "neighbour count must be capped and greater diversity ranked better");

    const std::vector<router::HgsFitness> eliteFitness =
        router::computeHgsFitness(
            segmentCatalog, fitnessIndividuals, 1, 10);
    expect(
        eliteFitness[0].biasedFitness == eliteFitness[0].costRank &&
            eliteFitness[1].biasedFitness == eliteFitness[1].costRank,
        "small elite-only populations must not get a negative diversity weight");

    const std::vector<router::HgsFitness> singleFitness =
        router::computeHgsFitness(
            segmentCatalog, {populationSeed}, 3, 1);
    expect(
        singleFitness.size() == 1 &&
            singleFitness[0].diversityContribution == 0.0 &&
            singleFitness[0].biasedFitness == 1.0,
        "single-member fitness must avoid division by zero");
    expect(
        router::computeHgsFitness(segmentCatalog, {}, 3, 1).empty(),
        "empty subpopulation must return empty fitness");

    const std::vector<router::HgsFitness> infeasibleFitness =
        router::computeHgsFitness(
            segmentCatalog, population.infeasibleIndividuals(), 3, 0);
    expect(
        infeasibleFitness.size() == 2 &&
            infeasibleFitness[0].diversityContribution == 0.0,
        "duplicate infeasible neighbours must give zero diversity");

    expectInvalidArgument(
        [&]()
        {
            router::computeHgsFitness(
                segmentCatalog, fitnessIndividuals, 0, 1);
        },
        "fitness must reject zero closest neighbours");
    expectInvalidArgument(
        [&]()
        {
            router::computeHgsFitness(
                segmentCatalog,
                {populationSeed, infeasiblePopulationIndividual}, 1, 0);
        },
        "fitness must reject mixed feasibility subpopulations");
    router::HgsIndividual nonFiniteFitnessIndividual = betterPopulationIndividual;
    nonFiniteFitnessIndividual.evaluation.penalizedCost =
        std::numeric_limits<double>::quiet_NaN();
    expectInvalidArgument(
        [&]()
        {
            router::computeHgsFitness(
                segmentCatalog, {nonFiniteFitnessIndividual}, 1, 0);
        },
        "fitness must reject non-finite penalized costs");

    router::HgsPopulation survivorPopulation(segmentCatalog.size());
    survivorPopulation.addIndividual(populationSeed);
    survivorPopulation.addIndividual(betterPopulationIndividual);
    survivorPopulation.addIndividual(populationSeed);
    survivorPopulation.addIndividual(tiedPopulationIndividual);
    survivorPopulation.addIndividual(infeasiblePopulationIndividual);
    survivorPopulation.addIndividual(infeasiblePopulationIndividual);
    survivorPopulation.addIndividual(infeasiblePopulationIndividual);
    expect(
        survivorPopulation.selectSurvivors(segmentCatalog, 2, 1, 0) == 3 &&
            survivorPopulation.feasibleIndividuals().size() == 2 &&
            survivorPopulation.infeasibleIndividuals().size() == 2,
        "survivor selection must trim each subpopulation independently");
    expect(
        survivorPopulation.feasibleIndividuals()[0].giantTour ==
                populationSeed.giantTour &&
            survivorPopulation.feasibleIndividuals()[0]
                    .decodedSolution.routes.size() == 3 &&
            survivorPopulation.feasibleIndividuals()[1].giantTour ==
                betterPopulationIndividual.giantTour,
        "clones must be removed before a unique worse-fitness member");
    expect(
        survivorPopulation.selectSurvivors(segmentCatalog, 1, 1, 1) == 2 &&
            survivorPopulation.feasibleIndividuals().size() == 1 &&
            survivorPopulation.feasibleIndividuals()[0].giantTour ==
                betterPopulationIndividual.giantTour &&
            survivorPopulation.infeasibleIndividuals().size() == 1,
        "selection without clones must remove the worst biased fitness");
    expect(
        survivorPopulation.bestFeasible().has_value() &&
            survivorPopulation.bestFeasible()->giantTour ==
                betterPopulationIndividual.giantTour &&
            survivorPopulation.bestFeasible()->evaluation.objectiveCost == 45.0,
        "survivor selection must preserve the historical feasible incumbent");
    expect(
        survivorPopulation.selectSurvivors(segmentCatalog, 3, 1, 0) == 0,
        "selection must not grow populations below the target");

    router::HgsPopulation routeOrderPopulation(segmentCatalog.size());
    routeOrderPopulation.addIndividual(betterPopulationIndividual);
    routeOrderPopulation.addIndividual(
        router::makeHgsIndividualFromSolution(
            populationInstance, segmentCatalog, reorderedRoutes, zeroPenalties));
    routeOrderPopulation.addIndividual(populationSeed);
    expect(
        routeOrderPopulation.selectSurvivors(segmentCatalog, 2, 1, 0) == 1 &&
            routeOrderPopulation.feasibleIndividuals()[0].giantTour ==
                betterPopulationIndividual.giantTour &&
            routeOrderPopulation.feasibleIndividuals()[1]
                    .decodedSolution.routes.size() == 3,
        "reordered routes must count as clones and exact ties retain the earlier member");

    router::HgsPopulation emptySurvivorPopulation(segmentCatalog.size());
    expect(
        emptySurvivorPopulation.selectSurvivors(segmentCatalog, 2, 1, 0) == 0,
        "empty survivor selection must be a no-op");
    expectInvalidArgument(
        [&]()
        {
            survivorPopulation.selectSurvivors(segmentCatalog, 0, 1, 0);
        },
        "survivor selection must reject zero target size");
    expectInvalidArgument(
        [&]()
        {
            survivorPopulation.selectSurvivors(segmentCatalog, 1, 0, 0);
        },
        "survivor selection must reject zero closest neighbours");
    expectInvalidArgument(
        [&]()
        {
            survivorPopulation.selectSurvivors({}, 1, 1, 0);
        },
        "survivor selection must reject a mismatched catalog size");
    expect(
        survivorPopulation.feasibleIndividuals().size() == 1 &&
            survivorPopulation.infeasibleIndividuals().size() == 1,
        "invalid selection parameters must not change population sizes");

    router::HgsPopulation tournamentPopulation(segmentCatalog.size());
    tournamentPopulation.addIndividual(populationSeed);
    tournamentPopulation.addIndividual(betterPopulationIndividual);
    tournamentPopulation.addIndividual(tiedPopulationIndividual);
    tournamentPopulation.addIndividual(infeasiblePopulationIndividual);
    const auto feasibleTournamentFitness = router::computeHgsFitness(
        segmentCatalog, tournamentPopulation.feasibleIndividuals(), 1, 1);
    const auto infeasibleTournamentFitness = router::computeHgsFitness(
        segmentCatalog, tournamentPopulation.infeasibleIndividuals(), 1, 1);
    const auto tournamentMember = [&](std::size_t index)
        -> const router::HgsIndividual &
    {
        return index < 3
                   ? tournamentPopulation.feasibleIndividuals()[index]
                   : tournamentPopulation.infeasibleIndividuals()[index - 3];
    };
    const auto tournamentFitness = [&](std::size_t index)
    {
        return index < 3
                   ? feasibleTournamentFitness[index].biasedFitness
                   : infeasibleTournamentFitness[index - 3].biasedFitness;
    };
    std::mt19937 tournamentRng(42);
    std::mt19937 expectedTournamentRng(42);
    std::uniform_int_distribution<std::size_t> tournamentDraw(0, 3);
    bool selectedFeasibleParent = false;
    bool selectedInfeasibleParent = false;
    bool testedTournamentTie = false;
    for (int trial = 0; trial < 64; ++trial)
    {
        const std::size_t first = tournamentDraw(expectedTournamentRng);
        const std::size_t second = tournamentDraw(expectedTournamentRng);
        const std::size_t expectedWinner =
            tournamentFitness(first) <= tournamentFitness(second)
                ? first
                : second;
        const router::HgsIndividual &parent = tournamentPopulation.selectParent(
            segmentCatalog, 1, 1, tournamentRng);
        expect(
            &parent == &tournamentMember(expectedWinner),
            "tournament must sample the union and choose the lower biased fitness");
        selectedFeasibleParent |= parent.evaluation.feasible;
        selectedInfeasibleParent |= !parent.evaluation.feasible;
        testedTournamentTie |=
            first != second &&
            tournamentFitness(first) == tournamentFitness(second);
    }
    expect(
        selectedFeasibleParent && selectedInfeasibleParent &&
            testedTournamentTie && tournamentRng == expectedTournamentRng,
        "seeded tournaments must cover both subpopulations and first-draw tie handling");
    expect(
        tournamentPopulation.feasibleIndividuals().size() == 3 &&
            tournamentPopulation.infeasibleIndividuals().size() == 1 &&
            tournamentPopulation.bestFeasible()->evaluation.objectiveCost == 45.0,
        "parent selection must not change population or feasible incumbent");

    router::HgsPopulation singleFeasibleParent(segmentCatalog.size());
    singleFeasibleParent.addIndividual(populationSeed);
    expect(
        &singleFeasibleParent.selectParent(segmentCatalog, 1, 1, tournamentRng) ==
            &singleFeasibleParent.feasibleIndividuals().front(),
        "single feasible member must be selectable without infeasible members");
    router::HgsPopulation singleInfeasibleParent(segmentCatalog.size());
    singleInfeasibleParent.addIndividual(infeasiblePopulationIndividual);
    expect(
        &singleInfeasibleParent.selectParent(segmentCatalog, 1, 1, tournamentRng) ==
            &singleInfeasibleParent.infeasibleIndividuals().front(),
        "single infeasible member must be selectable without feasible members");
    expectInvalidArgument(
        [&]()
        {
            emptySurvivorPopulation.selectParent(
                segmentCatalog, 1, 1, tournamentRng);
        },
        "parent selection must reject an empty population");
    expectInvalidArgument(
        [&]()
        {
            tournamentPopulation.selectParent({}, 1, 1, tournamentRng);
        },
        "parent selection must reject a mismatched catalog size");
    expectInvalidArgument(
        [&]()
        {
            tournamentPopulation.selectParent(
                segmentCatalog, 0, 1, tournamentRng);
        },
        "parent selection must reject zero closest neighbours");

    const std::vector<router::HgsGene> oxFirst =
        router::makeCanonicalGiantTour(6);
    std::vector<router::HgsGene> oxSecond = oxFirst;
    std::reverse(oxSecond.begin(), oxSecond.end());
    const std::vector<router::HgsGene> expectedOx = {
        {4}, {1}, {2}, {3}, {0}, {5}};
    expect(
        router::orderedCrossover(oxFirst, oxSecond, 1, 4) == expectedOx,
        "OX must retain the first-parent segment and cyclic second-parent order");
    expect(
        router::orderedCrossover(oxFirst, oxSecond, 0, 6) == oxFirst,
        "full-range OX must return the first parent");
    expect(
        router::orderedCrossover(oxFirst, oxFirst, 1, 4) == oxFirst,
        "identical parents must retain their permutation");
    for (std::size_t begin = 0; begin < oxFirst.size(); ++begin)
    {
        for (std::size_t end = begin + 1; end <= oxFirst.size(); ++end)
        {
            const auto child =
                router::orderedCrossover(oxFirst, oxSecond, begin, end);
            expect(
                router::isCompleteGiantTourPermutation(child, oxFirst.size()) &&
                    std::equal(
                        oxFirst.begin() + static_cast<std::ptrdiff_t>(begin),
                        oxFirst.begin() + static_cast<std::ptrdiff_t>(end),
                        child.begin() + static_cast<std::ptrdiff_t>(begin)),
                "every valid OX segment must preserve each gene exactly once");
        }
    }
    expect(
        oxFirst == router::makeCanonicalGiantTour(6) &&
            oxSecond.front().visitIndex == 5 &&
            oxSecond.back().visitIndex == 0,
        "crossover must not change either parent");

    std::mt19937 oxRng(42);
    std::mt19937 expectedOxRng(42);
    std::uniform_int_distribution<std::size_t> oxDraw(0, 5);
    for (int trial = 0; trial < 64; ++trial)
    {
        const std::size_t firstCut = oxDraw(expectedOxRng);
        const std::size_t secondCut = oxDraw(expectedOxRng);
        expect(
            router::orderedCrossover(oxFirst, oxSecond, oxRng) ==
                router::orderedCrossover(
                    oxFirst, oxSecond,
                    std::min(firstCut, secondCut),
                    std::max(firstCut, secondCut) + 1),
            "seeded OX must use two draws and match explicit cut points");
    }
    expect(
        oxRng == expectedOxRng,
        "OX must use the caller-owned RNG reproducibly");
    const std::vector<router::HgsGene> oneGene = {{0}};
    expect(
        router::orderedCrossover(oneGene, oneGene, oxRng) == oneGene &&
            router::orderedCrossover({}, {}, oxRng).empty() &&
            router::orderedCrossover({}, {}, 0, 0).empty(),
        "OX must support single-gene and empty permutations");

    expectInvalidArgument(
        [&]() { router::orderedCrossover(oxFirst, oxSecond, 2, 2); },
        "OX must reject empty segments for non-empty tours");
    expectInvalidArgument(
        [&]() { router::orderedCrossover(oxFirst, oxSecond, 4, 2); },
        "OX must reject reversed ranges");
    expectInvalidArgument(
        [&]() { router::orderedCrossover(oxFirst, oxSecond, 0, 7); },
        "OX must reject out-of-range segments");
    expectInvalidArgument(
        [&]() { router::orderedCrossover(oxFirst, oneGene, oxRng); },
        "OX must reject different parent sizes");
    auto duplicateOxParent = oxSecond;
    duplicateOxParent[0] = duplicateOxParent[1];
    expectInvalidArgument(
        [&]() { router::orderedCrossover(oxFirst, duplicateOxParent, 1, 4); },
        "OX must reject duplicate parent genes");
    auto outOfRangeOxParent = oxFirst;
    outOfRangeOxParent[0].visitIndex = oxFirst.size();
    expectInvalidArgument(
        [&]() { router::orderedCrossover(outOfRangeOxParent, oxSecond, oxRng); },
        "OX must reject out-of-range parent genes");

    router::Instance pipelineInstance = threeVehicleInstance;
    pipelineInstance.nodes[1].demandWeight = 12.0;
    pipelineInstance.nodes[1].demandVolume = 0.8;
    pipelineInstance.nodes[2].demandWeight = 5.0;
    pipelineInstance.nodes[2].demandVolume = 0.7;
    const std::vector<router::Visit> pipelineCatalog =
        router::splitCustomers(pipelineInstance);
    router::Solution pipelineSeed;
    for (const router::Visit &visit : pipelineCatalog)
    {
        pipelineSeed.routes.push_back({{visit}});
    }
    router::Solution reversedPipelineSeed = pipelineSeed;
    std::reverse(
        reversedPipelineSeed.routes.begin(), reversedPipelineSeed.routes.end());
    router::Solution infeasiblePipelineSeed;
    infeasiblePipelineSeed.routes = {
        {{pipelineCatalog[0], pipelineCatalog[2]}},
        {{pipelineCatalog[1]}}};
    expect(
        pipelineCatalog.size() == 3 &&
            router::validate(pipelineInstance, pipelineSeed).feasible &&
            !router::validate(pipelineInstance, infeasiblePipelineSeed).feasible,
        "pipeline fixture must use real split chunks and both feasibility classes");

    for (bool zoned : {false, true})
    {
        const double pipelineZonePenalty = zoned ? 50.0 : 0.0;
        const auto encodePipelineSolution = [&](const router::Solution &solution)
        {
            return zoned
                       ? router::makeHgsIndividualFromSolution(
                             pipelineInstance, pipelineCatalog, solution,
                             penaltyWeights, zoneOf, pipelineZonePenalty)
                       : router::makeHgsIndividualFromSolution(
                             pipelineInstance, pipelineCatalog, solution,
                             penaltyWeights);
        };
        router::HgsPopulation pipelinePopulation(pipelineCatalog.size());
        pipelinePopulation.addIndividual(encodePipelineSolution(pipelineSeed));
        pipelinePopulation.addIndividual(
            encodePipelineSolution(reversedPipelineSeed));
        pipelinePopulation.addIndividual(
            encodePipelineSolution(infeasiblePipelineSeed));
        std::mt19937 pipelineRng(42);
        for (int generation = 0; generation < 32; ++generation)
        {
            const auto &firstParent = pipelinePopulation.selectParent(
                pipelineCatalog, 1, 1, pipelineRng);
            const auto &secondParent = pipelinePopulation.selectParent(
                pipelineCatalog, 1, 1, pipelineRng);
            const auto offspringTour = router::orderedCrossover(
                firstParent.giantTour, secondParent.giantTour, pipelineRng);
            const router::HgsIndividual offspring = zoned
                ? router::decodeGiantTour(
                      pipelineInstance, pipelineCatalog, offspringTour,
                      penaltyWeights, zoneOf, pipelineZonePenalty)
                : router::decodeGiantTour(
                      pipelineInstance, pipelineCatalog, offspringTour,
                      penaltyWeights);
            expect(
                router::isCompleteGiantTourPermutation(
                    offspring.giantTour, pipelineCatalog.size()) &&
                    offspring.giantTour == offspringTour &&
                    router::encodeSolutionAsGiantTour(
                        offspring.decodedSolution, pipelineCatalog) ==
                        offspringTour,
                "parent-OX-Split pipeline must preserve every chunk and tour order");
            const auto validation =
                router::validate(pipelineInstance, offspring.decodedSolution);
            const auto reevaluated =
                encodePipelineSolution(offspring.decodedSolution);
            expect(
                validation.feasible == offspring.evaluation.feasible &&
                    offspring.decodedSolution.routes.size() <=
                        static_cast<std::size_t>(pipelineInstance.fleet.size) &&
                    std::abs(
                        reevaluated.evaluation.penalizedCost -
                        offspring.evaluation.penalizedCost) < 1e-9 &&
                    std::abs(
                        offspring.evaluation.objectiveCost -
                        offspring.evaluation.distanceCost -
                        pipelineZonePenalty *
                            offspring.evaluation.routeZoneExcess) < 1e-9,
                "decoded offspring must match validation and route-based evaluation");
            pipelinePopulation.addIndividual(offspring);
            pipelinePopulation.selectSurvivors(pipelineCatalog, 6, 1, 1);
        }
        expect(
            pipelinePopulation.bestFeasible().has_value() &&
                router::validate(
                    pipelineInstance,
                    pipelinePopulation.bestFeasible()->decodedSolution).feasible &&
                pipelinePopulation.feasibleIndividuals().size() <= 6 &&
                pipelinePopulation.infeasibleIndividuals().size() <= 6,
            "pipeline must retain a valid incumbent and bounded subpopulations");
    }

    const router::HgsPenaltyWeights educationPenalties = {100.0, 1000.0, 100.0};
    const auto repairedByRelocate = router::educateHgsRelocate(
        populationInstance, segmentCatalog,
        infeasiblePopulationIndividual, educationPenalties, 20);
    expect(
        repairedByRelocate.evaluation.feasible &&
            router::validate(
                populationInstance, repairedByRelocate.decodedSolution).feasible &&
            repairedByRelocate.evaluation.penalizedCost <
                router::makeHgsIndividualFromSolution(
                    populationInstance, segmentCatalog,
                    infeasiblePopulationIndividual.decodedSolution,
                    educationPenalties).evaluation.penalizedCost,
        "penalized relocate must improve an infeasible individual and restore feasibility");
    const auto relaxedEducation = router::educateHgsRelocate(
        populationInstance, segmentCatalog, populationSeed, zeroPenalties, 20);
    expect(
        !relaxedEducation.evaluation.feasible &&
            relaxedEducation.evaluation.penalizedCost < 60.0 &&
            router::isCompleteGiantTourPermutation(
                relaxedEducation.giantTour, segmentCatalog.size()),
        "education must allow beneficial soft violations while retaining every chunk");

    const auto unchangedEducation = router::educateHgsRelocate(
        populationInstance, segmentCatalog,
        infeasiblePopulationIndividual, educationPenalties, 0);
    expect(
        unchangedEducation.decodedSolution.routes.size() ==
            infeasiblePopulationIndividual.decodedSolution.routes.size() &&
            unchangedEducation.giantTour == infeasiblePopulationIndividual.giantTour &&
            unchangedEducation.evaluation.penalizedCost >
                infeasiblePopulationIndividual.evaluation.penalizedCost,
        "zero move budget must preserve routes but refresh scores with supplied penalties");
    const auto openedEducation = router::educateHgsRelocate(
        pipelineInstance, pipelineCatalog,
        router::makeHgsIndividualFromSolution(
            pipelineInstance, pipelineCatalog, infeasiblePipelineSeed,
            educationPenalties), educationPenalties, 20);
    expect(
        openedEducation.evaluation.feasible &&
            openedEducation.decodedSolution.routes.size() == 3 &&
            router::validate(pipelineInstance, openedEducation.decodedSolution).feasible,
        "relocate must open an unused vehicle to repair capacity violations");

    const auto zonedEducation = router::educateHgsRelocate(
        populationInstance, segmentCatalog, higherZoneCostIndividual,
        educationPenalties, zoneOf, 20.0, 20);
    expect(
        zonedEducation.evaluation.penalizedCost <=
            higherZoneCostIndividual.evaluation.penalizedCost &&
            std::abs(zonedEducation.evaluation.objectiveCost -
                     zonedEducation.evaluation.distanceCost -
                     20.0 * zonedEducation.evaluation.routeZoneExcess) < 1e-9,
        "education must optimize the same route-zone objective as Split");
    expect(
        infeasiblePopulationIndividual.evaluation.violations.timeWarp > 0.0 &&
            populationSeed.decodedSolution.routes.size() == 3,
        "education must not mutate its input individual");

    router::Instance exchangeInstance;
    exchangeInstance.n = 4;
    exchangeInstance.horizon = 1000.0;
    exchangeInstance.fleet = {2, 2.0, 2.0};
    exchangeInstance.nodes.resize(5);
    exchangeInstance.distanceMatrix.assign(5, std::vector<double>(5, 1.0));
    exchangeInstance.durationMatrix.assign(5, std::vector<double>(5, 1.0));
    for (int node = 0; node <= 4; ++node)
    {
        exchangeInstance.distanceMatrix[node][node] = 0.0;
        exchangeInstance.durationMatrix[node][node] = 0.0;
        if (node > 0)
        {
            exchangeInstance.nodes[node].demandWeight = 1.0;
            exchangeInstance.nodes[node].demandVolume = 0.1;
            exchangeInstance.nodes[node].twEnd = 1000;
            exchangeInstance.distanceMatrix[0][node] = 10.0;
            exchangeInstance.distanceMatrix[node][0] = 10.0;
        }
    }
    exchangeInstance.distanceMatrix[1][2] = 100.0;
    exchangeInstance.distanceMatrix[2][1] = 100.0;
    exchangeInstance.distanceMatrix[3][4] = 100.0;
    exchangeInstance.distanceMatrix[4][3] = 100.0;
    const auto exchangeCatalog = router::splitCustomers(exchangeInstance);
    router::Solution exchangeSolution;
    exchangeSolution.routes = {
        {{exchangeCatalog[0], exchangeCatalog[1]}},
        {{exchangeCatalog[2], exchangeCatalog[3]}}};
    const router::HgsPenaltyWeights exchangePenalties = {10000.0, 10000.0, 10000.0};
    const auto exchangeSeed = router::makeHgsIndividualFromSolution(
        exchangeInstance, exchangeCatalog, exchangeSolution, exchangePenalties);
    const auto relocateBlocked = router::educateHgsRelocate(
        exchangeInstance, exchangeCatalog, exchangeSeed, exchangePenalties, 20);
    const auto exchangeEducated = router::educateHgsIndividual(
        exchangeInstance, exchangeCatalog, exchangeSeed, exchangePenalties, 20);
    expect(
        relocateBlocked.evaluation.penalizedCost == exchangeSeed.evaluation.penalizedCost &&
            exchangeEducated.evaluation.penalizedCost == 42.0 &&
            router::validate(exchangeInstance, exchangeEducated.decodedSolution).feasible,
        "combined education must escape a relocate local optimum using exchange");
    const auto exchangeFixedPoint = router::educateHgsIndividual(
        exchangeInstance, exchangeCatalog, exchangeEducated, exchangePenalties, 20);
    expect(
        exchangeFixedPoint.evaluation.penalizedCost == exchangeEducated.evaluation.penalizedCost,
        "completed education must be stable when no neighbourhood improves");

    const std::vector<int> exchangeZones = {-1, 0, 1, 1, 0};
    const auto zonedExchangeSeed = router::makeHgsIndividualFromSolution(
        exchangeInstance, exchangeCatalog, exchangeSolution,
        exchangePenalties, exchangeZones, 20.0);
    const auto zonedExchange = router::educateHgsIndividual(
        exchangeInstance, exchangeCatalog, zonedExchangeSeed,
        exchangePenalties, exchangeZones, 20.0, 20);
    expect(
        zonedExchange.evaluation.penalizedCost < zonedExchangeSeed.evaluation.penalizedCost &&
            zonedExchange.evaluation.routeZoneExcess == 0 &&
            router::isCompleteGiantTourPermutation(zonedExchange.giantTour, exchangeCatalog.size()) &&
            router::validate(exchangeInstance, zonedExchange.decodedSolution).feasible,
        "all education moves must preserve coverage and use the route-zone objective");
    const auto combinedRepair = router::educateHgsIndividual(
        populationInstance, segmentCatalog,
        infeasiblePopulationIndividual, educationPenalties, 20);
    const auto combinedZeroBudget = router::educateHgsIndividual(
        populationInstance, segmentCatalog,
        infeasiblePopulationIndividual, educationPenalties, 0);
    expect(
        combinedRepair.evaluation.feasible &&
            combinedZeroBudget.giantTour == infeasiblePopulationIndividual.giantTour &&
            combinedRepair.evaluation.penalizedCost < combinedZeroBudget.evaluation.penalizedCost,
        "combined education must support infeasible starts and respect zero move budgets");
    expect(
        exchangeSeed.evaluation.penalizedCost == 240.0 &&
            exchangeSeed.decodedSolution.routes[0].stops[0].nodeIndex == 1,
        "combined education must not mutate its input");

    const auto checkDirectedEducation = [&](
        const std::vector<std::vector<double>> &distances,
        const std::vector<std::vector<int>> &routeNodes,
        int capacity, double seedCost, double improvedCost)
    {
        router::Instance inst;
        inst.n = static_cast<int>(distances.size()) - 1;
        inst.horizon = 1000.0;
        inst.fleet = {
            static_cast<int>(routeNodes.size()),
            static_cast<double>(capacity), static_cast<double>(capacity)};
        inst.nodes.resize(distances.size());
        inst.distanceMatrix = distances;
        inst.durationMatrix.assign(
            distances.size(), std::vector<double>(distances.size(), 1.0));
        for (std::size_t node = 0; node < distances.size(); ++node)
        {
            inst.durationMatrix[node][node] = 0.0;
            if (node > 0)
            {
                inst.nodes[node].demandWeight = 1.0;
                inst.nodes[node].demandVolume = 0.1;
                inst.nodes[node].twEnd = 1000;
            }
        }
        const auto catalog = router::splitCustomers(inst);
        router::Solution solution;
        for (const auto &nodes : routeNodes)
        {
            router::Route route;
            for (int node : nodes)
            {
                route.stops.push_back(catalog[static_cast<std::size_t>(node - 1)]);
            }
            solution.routes.push_back(std::move(route));
        }
        const router::HgsPenaltyWeights penalties = {10000.0, 10000.0, 10000.0};
        const auto seed = router::makeHgsIndividualFromSolution(
            inst, catalog, solution, penalties);
        const auto relocated = router::educateHgsRelocate(
            inst, catalog, seed, penalties, 20);
        const auto educated = router::educateHgsIndividual(
            inst, catalog, seed, penalties, 1);
        expect(
            seed.evaluation.penalizedCost == seedCost &&
                relocated.evaluation.penalizedCost == seedCost &&
                educated.evaluation.penalizedCost == improvedCost &&
                router::validate(inst, educated.decodedSolution).feasible &&
                router::isCompleteGiantTourPermutation(
                    educated.giantTour, catalog.size()),
            "directed 2-opt/2-opt-star fixtures must improve with exactly one accepted move");
    };
    checkDirectedEducation(
        {
            {0, 10, 10, 10, 10, 10, 10},
            {10, 0, 10, 11, 8, 11, 6},
            {10, 3, 0, 17, 4, 17, 17},
            {10, 7, 12, 0, 12, 5, 8},
            {10, 4, 5, 9, 0, 7, 6},
            {10, 20, 5, 3, 6, 0, 16},
            {10, 15, 19, 19, 15, 19, 0}
        },
        {{2, 4, 5, 3, 1, 6}}, 6, 47.0, 45.0);
    checkDirectedEducation(
        {
            {0, 10, 10, 10, 10, 10, 10, 10, 10},
            {10, 0, 18, 14, 8, 11, 8, 14, 11},
            {10, 9, 0, 3, 19, 12, 4, 17, 2},
            {10, 6, 8, 0, 17, 2, 13, 3, 15},
            {10, 10, 10, 11, 0, 3, 18, 15, 1},
            {10, 12, 7, 10, 19, 0, 10, 20, 8},
            {10, 15, 12, 19, 16, 7, 0, 18, 8},
            {10, 5, 1, 14, 1, 8, 18, 0, 12},
            {10, 1, 11, 1, 13, 10, 4, 7, 0}
        },
        {{7, 2, 3, 1}, {4, 8, 6, 5}}, 4, 62.0, 60.0);

    const router::HgsPenaltyWeights weakTimePenalty = {0.0, 0.0, 0.01};
    const auto repairStart = router::makeHgsIndividualFromSolution(
        populationInstance, segmentCatalog,
        infeasiblePopulationIndividual.decodedSolution, weakTimePenalty);
    const auto repairResult = router::repairHgsIndividual(
        populationInstance, segmentCatalog, repairStart, weakTimePenalty, 20);
    expect(
        repairResult.evaluation.feasible &&
            router::validate(populationInstance, repairResult.decodedSolution).feasible &&
            repairResult.evaluation.penalizedCost == repairResult.evaluation.objectiveCost,
        "repair must restore feasibility and return scores under original penalties");
    const auto failedRepair = router::repairHgsIndividual(
        populationInstance, segmentCatalog, repairStart, weakTimePenalty, 0);
    expect(
        !failedRepair.evaluation.feasible &&
            failedRepair.giantTour == repairStart.giantTour &&
            failedRepair.evaluation.penalizedCost == repairStart.evaluation.penalizedCost,
        "failed repair must retain an original-scale non-worsening candidate");
    const auto alreadyFeasibleRepair = router::repairHgsIndividual(
        populationInstance, segmentCatalog,
        betterPopulationIndividual, weakTimePenalty, 20);
    expect(
        alreadyFeasibleRepair.giantTour == betterPopulationIndividual.giantTour,
        "repair must leave an already feasible solution unchanged");
    const auto zonedRepairStart = router::makeHgsIndividualFromSolution(
        populationInstance, segmentCatalog, repairStart.decodedSolution,
        weakTimePenalty, zoneOf, 20.0);
    const auto zonedRepair = router::repairHgsIndividual(
        populationInstance, segmentCatalog, zonedRepairStart,
        weakTimePenalty, zoneOf, 20.0, 20);
    expect(
        zonedRepair.evaluation.feasible &&
            std::abs(zonedRepair.evaluation.objectiveCost -
                     zonedRepair.evaluation.distanceCost -
                     20.0 * zonedRepair.evaluation.routeZoneExcess) < 1e-9,
        "repair must keep the zone objective unchanged");

    const router::HgsPenaltyControlOptions controlOptions = {
        4, 0.5, 0.0, 2.0, 0.5, 1.0, 100.0};
    router::HgsPenaltyController control({10.0, 20.0, 40.0}, controlOptions);
    for (int sample = 0; sample < 3; ++sample)
    {
        expect(!control.observe({1.0, 0.0, 0.0}),
               "adaptive penalties must wait for a complete observation window");
    }
    expect(
        control.observe({1.0, 0.0, 0.0}) &&
            control.weights().weightPenalty == 20.0 &&
            control.weights().volumePenalty == 10.0 &&
            control.weights().timeWarpPenalty == 20.0,
        "adaptive penalties must adjust weight, volume and time independently");
    for (int sample = 0; sample < 4; ++sample)
    {
        expect(
            !control.observe(sample < 2
                                 ? router::HgsConstraintViolations{1.0, 1.0, 1.0}
                                 : router::HgsConstraintViolations{}),
            "target feasibility rates must retain penalties and reset window counts");
    }
    router::HgsPenaltyController boundedControl({90.0, 2.0, 1.0}, controlOptions);
    for (int sample = 0; sample < 4; ++sample)
    {
        boundedControl.observe({1.0, 0.0, 0.0});
    }
    expect(
        boundedControl.weights().weightPenalty == 100.0 &&
            boundedControl.weights().volumePenalty == 1.0 &&
            boundedControl.weights().timeWarpPenalty == 1.0,
        "adaptive penalties must respect upper and lower bounds");
    router::HgsPenaltyControlOptions boundaryOptions;
    boundaryOptions.windowSize = 20;
    router::HgsPenaltyController boundaryControl({10.0, 10.0, 10.0}, boundaryOptions);
    for (int feasibleSamples : {3, 5})
    {
        for (int sample = 0; sample < 20; ++sample)
        {
            expect(
                !boundaryControl.observe(sample < feasibleSamples
                    ? router::HgsConstraintViolations{}
                    : router::HgsConstraintViolations{1.0, 1.0, 1.0}),
                "deadband boundaries must not trigger floating-point updates");
        }
    }

    router::HgsPopulation reweightedPopulation(segmentCatalog.size());
    reweightedPopulation.addIndividual(betterPopulationIndividual);
    reweightedPopulation.addIndividual(infeasiblePopulationIndividual);
    reweightedPopulation.updatePenaltyWeights(educationPenalties);
    const double expectedReweightedCost =
        router::makeHgsIndividualFromSolution(
            populationInstance, segmentCatalog,
            infeasiblePopulationIndividual.decodedSolution,
            educationPenalties).evaluation.penalizedCost;
    expect(
        reweightedPopulation.infeasibleIndividuals()[0].evaluation.penalizedCost ==
                expectedReweightedCost &&
            reweightedPopulation.feasibleIndividuals().size() == 1 &&
            reweightedPopulation.infeasibleIndividuals().size() == 1 &&
            reweightedPopulation.bestFeasible()->evaluation.objectiveCost == 45.0,
        "population reweighting must refresh scores without changing routes or incumbent");

    auto invalidControlOptions = controlOptions;
    invalidControlOptions.windowSize = 0;
    expectInvalidArgument(
        [&]() { router::HgsPenaltyController bad({10.0, 10.0, 10.0}, invalidControlOptions); },
        "penalty controller must reject an empty window");
    expectInvalidArgument(
        [&]() { router::HgsPenaltyController bad(zeroPenalties, controlOptions); },
        "adaptive penalties must start positive and within bounds");
    expectInvalidArgument(
        [&]() { control.observe({std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}); },
        "penalty observations must reject NaN violations");
    expectInvalidArgument(
        [&]()
        {
            router::repairHgsIndividual(
                populationInstance, segmentCatalog, repairStart,
                {0.0, 0.0, std::numeric_limits<double>::infinity()}, 20);
        },
        "repair must reject non-finite penalty weights");
    expectRuntimeError(
        [&]()
        {
            reweightedPopulation.updatePenaltyWeights(
                {0.0, 0.0, std::numeric_limits<double>::max()});
        },
        "population reweighting must reject overflow");
    expect(
        reweightedPopulation.infeasibleIndividuals()[0].evaluation.penalizedCost ==
            expectedReweightedCost,
        "failed reweighting must not partially update population scores");

    router::HgsRunOptions runOptions;
    runOptions.mu = 2;
    runOptions.lambda = 3;
    runOptions.nClose = 1;
    runOptions.nElite = 1;
    runOptions.maxIterations = 6;
    runOptions.maxNonImprovingIterations = 100;
    runOptions.diversificationInterval = 0;
    runOptions.maxAcceptedEducationMoves = 10;
    runOptions.repairProbability = 1.0;
    runOptions.penaltyControl.windowSize = 2;
    const auto runResult = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, runOptions);
    const auto repeatedRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, runOptions);
    expect(
        runResult.iterations == 6 &&
            runResult.perturbedFillAttempts == 4 &&
            runResult.randomTourAttempts == 4 &&
            runResult.fillsCutByTime == 0 &&
            runResult.stopReason == router::HgsStopReason::IterationLimit &&
            runResult.bestFeasible.evaluation.feasible &&
            router::validate(pipelineInstance, runResult.bestFeasible.decodedSolution).feasible,
        "HGS must initialize, evolve and return a validated feasible incumbent");
    expect(
        runResult.bestFeasible.giantTour == repeatedRun.bestFeasible.giantTour &&
            runResult.bestFeasible.evaluation.objectiveCost ==
                repeatedRun.bestFeasible.evaluation.objectiveCost &&
            runResult.splitFailures == repeatedRun.splitFailures &&
            runResult.repairsAttempted == repeatedRun.repairsAttempted &&
            runResult.finalPenaltyWeights.timeWarpPenalty ==
                repeatedRun.finalPenaltyWeights.timeWarpPenalty,
        "iteration-limited HGS must be reproducible with the same seed and toolchain");
    expect(
        runResult.bestFeasible.evaluation.objectiveCost <=
            router::makeHgsIndividualFromSolution(
                pipelineInstance, pipelineCatalog, pipelineSeed,
                educationPenalties).evaluation.objectiveCost,
        "HGS must never lose the supplied feasible incumbent");

    auto stagnationOptions = runOptions;
    stagnationOptions.maxIterations = 20;
    stagnationOptions.maxNonImprovingIterations = 5;
    stagnationOptions.diversificationInterval = 2;
    const auto stagnantRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, stagnationOptions);
    expect(
        stagnantRun.stopReason == router::HgsStopReason::StagnationLimit &&
            stagnantRun.iterations == 5 && stagnantRun.diversifications == 2 &&
            router::validate(pipelineInstance, stagnantRun.bestFeasible.decodedSolution).feasible,
        "diversification must preserve the incumbent without resetting stagnation artificially");
    const auto zonedRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed,
        educationPenalties, runOptions, zoneOf, 50.0);
    expect(
        router::validate(pipelineInstance, zonedRun.bestFeasible.decodedSolution).feasible &&
            std::abs(zonedRun.bestFeasible.evaluation.objectiveCost -
                     zonedRun.bestFeasible.evaluation.distanceCost -
                     50.0 * zonedRun.bestFeasible.evaluation.routeZoneExcess) < 1e-9,
        "HGS loop must preserve the route-centric zone objective");
    auto seedOnlyOptions = runOptions;
    seedOnlyOptions.maxIterations = 0;
    const auto seedOnlyRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, seedOnlyOptions);
    expect(
        seedOnlyRun.iterations == 0 && seedOnlyRun.randomTourAttempts == 0 &&
            seedOnlyRun.perturbedFillAttempts == 0,
        "zero iteration budget must return the seed without generating individuals");

    router::HgsPopulation thresholdPopulation(segmentCatalog.size());
    thresholdPopulation.addIndividual(populationSeed);
    thresholdPopulation.addIndividual(betterPopulationIndividual);
    thresholdPopulation.addIndividual(populationSeed);
    thresholdPopulation.addIndividual(infeasiblePopulationIndividual);
    thresholdPopulation.addIndividual(infeasiblePopulationIndividual);
    expect(
        thresholdPopulation.selectSurvivors(segmentCatalog, 1, 1, 1, 3) == 2 &&
            thresholdPopulation.feasibleIndividuals().size() == 1 &&
            thresholdPopulation.infeasibleIndividuals().size() == 2,
        "threshold selection must only trim the subpopulation that reached its trigger");
    thresholdPopulation.retainBest(1);
    expect(
        thresholdPopulation.infeasibleIndividuals().size() == 1 &&
            thresholdPopulation.bestFeasible()->evaluation.objectiveCost == 45.0,
        "retention must keep the best members and historical incumbent");

    expectInvalidArgument(
        [&]()
        {
            router::runHgs(
                pipelineInstance, pipelineCatalog, infeasiblePipelineSeed,
                educationPenalties, runOptions);
        },
        "HGS must reject an infeasible seed instead of reporting success");
    auto invalidRunOptions = runOptions;
    invalidRunOptions.mu = 0;
    expectInvalidArgument(
        [&]()
        {
            router::runHgs(
                pipelineInstance, pipelineCatalog, pipelineSeed,
                educationPenalties, invalidRunOptions);
        },
        "HGS must reject invalid population options");
    for (const auto &mutate : std::vector<std::function<void(router::HgsRunOptions &)>>{
             [](router::HgsRunOptions &o) { o.perturbedFillFraction = -0.1; },
             [](router::HgsRunOptions &o) { o.perturbedFillFraction = 1.1; },
             [](router::HgsRunOptions &o) { o.perturbationStrength = std::numeric_limits<double>::quiet_NaN(); },
             [](router::HgsRunOptions &o) { o.fillTimeFraction = 0.0; },
             [](router::HgsRunOptions &o) { o.fillTimeFraction = 1.5; }})
    {
        auto badFillOptions = runOptions;
        mutate(badFillOptions);
        expectInvalidArgument(
            [&]()
            {
                router::runHgs(
                    pipelineInstance, pipelineCatalog, pipelineSeed,
                    educationPenalties, badFillOptions);
            },
            "HGS must reject invalid population fill options");
    }

    auto randomFillOptions = runOptions;
    randomFillOptions.perturbedFillFraction = 0.0;
    const auto randomFillRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, randomFillOptions);
    auto perturbedFillOptions = runOptions;
    perturbedFillOptions.perturbedFillFraction = 1.0;
    const auto perturbedFillRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, perturbedFillOptions);
    expect(
        randomFillRun.randomTourAttempts == 8 && randomFillRun.perturbedFillAttempts == 0 &&
            perturbedFillRun.randomTourAttempts == 0 && perturbedFillRun.perturbedFillAttempts == 8 &&
            router::validate(pipelineInstance, randomFillRun.bestFeasible.decodedSolution).feasible &&
            router::validate(pipelineInstance, perturbedFillRun.bestFeasible.decodedSolution).feasible,
        "population fill must split attempts between perturbed and random fills");
    expectInvalidArgument(
        [&]() { thresholdPopulation.selectSurvivors(segmentCatalog, 2, 1, 1, 2); },
        "survivor trigger must be above the target size");
    expectInvalidArgument(
        [&]() { thresholdPopulation.retainBest(0); },
        "retention must reject zero target size");

    auto noEducationOptions = runOptions;
    noEducationOptions.maxAcceptedEducationMoves = 0;
    const auto failedRepairRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed,
        {1.0, 1.0, 0.01}, noEducationOptions);
    expect(
        failedRepairRun.repairsAttempted > 0 &&
            failedRepairRun.repairsSucceeded == 0 &&
            failedRepairRun.bestFeasible.evaluation.objectiveCost == 60.0 &&
            router::validate(
                pipelineInstance, failedRepairRun.bestFeasible.decodedSolution).feasible,
        "failed repairs must not replace the independently validated feasible incumbent");

    router::Instance emptyRunInstance;
    emptyRunInstance.n = 0;
    emptyRunInstance.horizon = 1.0;
    emptyRunInstance.fleet = {0, 1.0, 1.0};
    emptyRunInstance.nodes.resize(1);
    emptyRunInstance.distanceMatrix = {{0.0}};
    emptyRunInstance.durationMatrix = {{0.0}};
    const auto emptyRun = router::runHgs(
        emptyRunInstance, {}, {}, educationPenalties, runOptions);
    expect(
        emptyRun.stopReason == router::HgsStopReason::EmptyInstance &&
            emptyRun.iterations == 0 && emptyRun.randomTourAttempts == 0 &&
            emptyRun.perturbedFillAttempts == 0 &&
            emptyRun.bestFeasible.decodedSolution.routes.empty(),
        "empty HGS instances must finish without crossover or population generation");

    using HgsClock = std::chrono::steady_clock;
    const router::HgsDeadline expiredDeadline =
        HgsClock::now() - std::chrono::nanoseconds(1);
    const auto expectTimeLimit = [&](auto action, const char *message)
    {
        try
        {
            action();
            expect(false, message);
        }
        catch (const router::HgsTimeLimitReached &)
        {
        }
        catch (...)
        {
            expect(false, message);
        }
    };
    auto expiredOptions = runOptions;
    expiredOptions.deadline = expiredDeadline;
    const auto expiredRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, expiredOptions);
    expect(
        expiredRun.stopReason == router::HgsStopReason::TimeLimit &&
            expiredRun.iterations == 0 && expiredRun.randomTourAttempts == 0 &&
            expiredRun.perturbedFillAttempts == 0 &&
            router::validate(pipelineInstance, expiredRun.bestFeasible.decodedSolution).feasible,
        "expired deadlines must return the validated seed without starting search");

    expectTimeLimit(
        [&]()
        {
            router::decodeGiantTour(
                pipelineInstance, pipelineCatalog,
                router::makeCanonicalGiantTour(pipelineCatalog.size()),
                educationPenalties, expiredDeadline);
        },
        "Split must honor deadlines independently of the generation loop");
    expectTimeLimit(
        [&]()
        {
            router::computeHgsFitness(
                segmentCatalog, {populationSeed, betterPopulationIndividual},
                1, 1, expiredDeadline);
        },
        "fitness computation must honor deadlines");
    expectTimeLimit(
        [&]()
        {
            thresholdPopulation.selectParent(segmentCatalog, 1, 1, oxRng, expiredDeadline);
        },
        "parent selection must honor deadlines");
    expectTimeLimit(
        [&]()
        {
            thresholdPopulation.selectSurvivors(segmentCatalog, 1, 1, 1, 0, expiredDeadline);
        },
        "survivor selection must honor deadlines");

    const auto timedEducationSeed = router::makeHgsIndividualFromSolution(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties);
    const auto expiredEducation = router::educateHgsIndividual(
        pipelineInstance, pipelineCatalog, timedEducationSeed,
        educationPenalties, 100, expiredDeadline);
    expect(
        expiredEducation.giantTour == timedEducationSeed.giantTour &&
            expiredEducation.evaluation.penalizedCost == timedEducationSeed.evaluation.penalizedCost,
        "interrupted education must return a complete consistently scored individual");
    const auto expiredRepair = router::repairHgsIndividual(
        populationInstance, segmentCatalog, repairStart,
        weakTimePenalty, 100, expiredDeadline);
    expect(
        expiredRepair.giantTour == repairStart.giantTour &&
            expiredRepair.evaluation.penalizedCost == repairStart.evaluation.penalizedCost,
        "interrupted repair must preserve original-scale penalties");

    auto futureOptions = runOptions;
    futureOptions.deadline = HgsClock::now() + std::chrono::hours(1);
    const auto futureRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, futureOptions);
    expect(
        futureRun.stopReason == router::HgsStopReason::IterationLimit &&
            futureRun.iterations == runResult.iterations &&
            futureRun.bestFeasible.giantTour == runResult.bestFeasible.giantTour &&
            futureRun.finalPenaltyWeights.timeWarpPenalty ==
                runResult.finalPenaltyWeights.timeWarpPenalty,
        "an unreached deadline must not alter seeded iteration-limited behavior");

    auto liveOptions = runOptions;
    liveOptions.mu = 1000;
    liveOptions.deadline = HgsClock::now() + std::chrono::milliseconds(1);
    const auto liveRun = router::runHgs(
        pipelineInstance, pipelineCatalog, pipelineSeed, educationPenalties, liveOptions);
    expect(
        liveRun.stopReason == router::HgsStopReason::TimeLimit &&
            router::validate(pipelineInstance, liveRun.bestFeasible.decodedSolution).feasible &&
            liveRun.bestFeasible.evaluation.objectiveCost <=
                timedEducationSeed.evaluation.objectiveCost,
        "a live deadline must stop search while preserving the feasible incumbent");

    const auto initialScalePenalties =
        router::makeInitialHgsPenalties(pipelineInstance, pipelineCatalog);
    expect(
        initialScalePenalties.weightPenalty == 1.0 &&
            std::abs(initialScalePenalties.volumePenalty - 10.0 / 0.7) < 1e-9 &&
            initialScalePenalties.timeWarpPenalty == 1.0,
        "initial penalties must normalize distance against chunk and time scales");
    auto initialBoundsOptions = router::HgsPenaltyControlOptions{};
    initialBoundsOptions.minPenalty = 2.0;
    initialBoundsOptions.maxPenalty = 4.0;
    const auto boundedPenalties = router::makeInitialHgsPenalties(
        pipelineInstance, pipelineCatalog, initialBoundsOptions);
    expect(
        boundedPenalties.weightPenalty == 2.0 &&
            boundedPenalties.volumePenalty == 4.0 &&
            boundedPenalties.timeWarpPenalty == 2.0,
        "initial penalties must respect adaptive-controller bounds");
    const auto emptyPenalties = router::makeInitialHgsPenalties(emptyRunInstance, {});
    expect(
        emptyPenalties.weightPenalty == 1.0 &&
            emptyPenalties.volumePenalty == 1.0 &&
            emptyPenalties.timeWarpPenalty == 1.0,
        "zero-distance empty instances must still receive positive initial penalties");
    auto invalidScaleInstance = pipelineInstance;
    invalidScaleInstance.distanceMatrix[0][1] = -1.0;
    expectInvalidArgument(
        [&]() { router::makeInitialHgsPenalties(invalidScaleInstance, pipelineCatalog); },
        "initial penalty scales must reject invalid matrix data");

    if (failures != 0)
    {
        std::cerr << failures << " HGS test(s) failed\n";
        return 1;
    }

    std::cout << "All HGS tests passed\n";
    return 0;
}
