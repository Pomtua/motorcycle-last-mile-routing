#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "router/cost.hpp"
#include "router/hgs.hpp"
#include "router/instance_io.hpp"
#include "router/local_search.hpp"
#include "router/solomon_i1.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"
#include "router/zones.hpp"

namespace
{
    using Clock = std::chrono::steady_clock;

    double elapsedMs(
        Clock::time_point start,
        Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(
                   end - start)
            .count();
    }

    router::HgsPenaltyWeights makeInitialPenalties(
        const router::Instance &inst,
        const std::vector<router::Visit> &visitCatalog)
    {
        double distanceScale = 0.0;
        double weightScale = 0.0;
        double volumeScale = 0.0;
        double timeScale = 0.0;

        for (const router::Visit &visit : visitCatalog)
        {
            weightScale = std::max(weightScale, visit.weight);
            volumeScale = std::max(volumeScale, visit.volume);
        }

        for (const auto &row : inst.distanceMatrix)
        {
            for (double distance : row)
            {
                distanceScale = std::max(distanceScale, distance);
            }
        }

        for (const auto &row : inst.durationMatrix)
        {
            for (double duration : row)
            {
                timeScale = std::max(timeScale, duration);
            }
        }

        for (const router::Node &node : inst.nodes)
        {
            timeScale = std::max(
                timeScale, static_cast<double>(node.serviceTime));
        }

        if (weightScale == 0.0)
        {
            weightScale = inst.fleet.weightCapacity;
        }
        if (volumeScale == 0.0)
        {
            volumeScale = inst.fleet.volumeCapacity;
        }
        if (timeScale == 0.0)
        {
            timeScale = inst.horizon;
        }

        for (double scale :
             {distanceScale, weightScale, volumeScale, timeScale})
        {
            if (!std::isfinite(scale) || scale <= 0.0)
            {
                throw std::invalid_argument(
                    "initial HGS penalty scales must be finite and positive");
            }
        }

        return {
            distanceScale / weightScale,
            distanceScale / volumeScale,
            distanceScale / timeScale};
    }
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr
            << "usage: bench_hgs_split "
               "<instance.json> [repetitions]\n";
        return 2;
    }

    try
    {
        const unsigned long repetitions =
            argc == 3 ? std::stoul(argv[2]) : 1UL;

        if (repetitions == 0 || repetitions > 100)
        {
            throw std::invalid_argument(
                "repetitions must be between 1 and 100");
        }

        const auto setupStart = Clock::now();

        const router::Instance inst =
            router::loadInstance(argv[1]);
        const std::vector<router::Visit> visitCatalog =
            router::splitCustomers(inst);
        const router::ZoneSelection zones =
            router::selectZones(inst);

        const router::Solution i1 =
            router::solomonI1(inst, false);

        const double distanceScale =
            router::computeCost(inst, i1) /
            static_cast<double>(
                std::max<std::size_t>(
                    i1.routes.size(), 1));
        const double zonePenalty =
            std::floor(0.10 * distanceScale + 0.5);

        const router::Solution seed =
            router::localSearch(
                inst,
                i1,
                zones.zoneOf,
                zonePenalty);

        if (!router::validate(inst, seed).feasible)
        {
            throw std::runtime_error(
                "I1+LS did not produce a feasible "
                "benchmark seed");
        }

        const std::vector<router::HgsGene> giantTour =
            router::encodeSolutionAsGiantTour(
                seed, visitCatalog);
        const router::HgsPenaltyWeights penalties =
            makeInitialPenalties(inst, visitCatalog);
        const auto setupEnd = Clock::now();

        std::cout << std::fixed
                  << std::setprecision(3)
                  << "instance=" << argv[1]
                  << " customers=" << inst.n
                  << " chunks=" << visitCatalog.size()
                  << " fleet=" << inst.fleet.size
                  << " zones=" << zones.numZones
                  << " seed_routes=" << seed.routes.size()
                  << " zone_penalty=" << zonePenalty
                  << " weight_penalty=" << penalties.weightPenalty
                  << " volume_penalty=" << penalties.volumePenalty
                  << " time_warp_penalty=" << penalties.timeWarpPenalty
                  << " setup_ms="
                  << elapsedMs(setupStart, setupEnd)
                  << std::endl;

        std::vector<double> decodeTimes;
        decodeTimes.reserve(repetitions);
        router::HgsIndividual last;

        for (unsigned long repetition = 0;
             repetition < repetitions;
             ++repetition)
        {
            const auto start = Clock::now();
            router::HgsIndividual decoded =
                router::decodeGiantTour(
                    inst,
                    visitCatalog,
                    giantTour,
                    penalties,
                    zones.zoneOf,
                    zonePenalty);
            const auto end = Clock::now();

            decodeTimes.push_back(
                elapsedMs(start, end));
            last = std::move(decoded);
        }

        std::sort(
            decodeTimes.begin(),
            decodeTimes.end());
        const std::size_t middle =
            decodeTimes.size() / 2;
        const double medianMs =
            decodeTimes.size() % 2 == 0
                ? (decodeTimes[middle - 1] +
                   decodeTimes[middle]) /
                      2.0
                : decodeTimes[middle];

        const router::ValidationReport validation =
            router::validate(
                inst, last.decodedSolution);

        const double seedDistanceCost =
            router::computeCost(inst, seed);
        const double seedZoneCost =
            router::computeRouteZoneCost(
                seed, zones.zoneOf, zonePenalty);
        const double seedObjectiveCost =
            seedDistanceCost + seedZoneCost;

        std::cout
            << "seed_distance_cost=" << seedDistanceCost
            << " seed_zone_cost=" << seedZoneCost
            << " seed_objective_cost=" << seedObjectiveCost
            << '\n';

        std::cout
            << "repetitions=" << repetitions
            << " decode_min_ms=" << decodeTimes.front()
            << " decode_median_ms=" << medianMs
            << " decoded_routes="
            << last.decodedSolution.routes.size()
            << " decoded_distance_cost="
            << last.evaluation.distanceCost
            << " decoded_zone_cost="
            << last.evaluation.routeZoneCost
            << " decoded_objective_cost="
            << last.evaluation.objectiveCost
            << " decoded_penalized_cost="
            << last.evaluation.penalizedCost
            << " penalized_gap_vs_seed="
            << (last.evaluation.penalizedCost -
                seedObjectiveCost)
            << " hgs_feasible="
            << std::boolalpha
            << last.evaluation.feasible
            << " validator_feasible="
            << validation.feasible
            << " validation_violations="
            << validation.violations.size()
            << std::scientific
            << std::setprecision(9)
            << " weight_excess="
            << last.evaluation.violations.weightExcess
            << " volume_excess="
            << last.evaluation.violations.volumeExcess
            << " time_warp="
            << last.evaluation.violations.timeWarp
            << '\n';

        for (const std::string &violation :
             validation.violations)
        {
            std::cout
                << "VALIDATION_VIOLATION "
                << violation << '\n';
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 2;
    }
}
