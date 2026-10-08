#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

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

    double elapsedMs(Clock::time_point start, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    std::uint32_t parseHgsSeed(const std::string &text)
    {
        if (text.empty() || !std::all_of(text.begin(), text.end(),
                [](char c) { return c >= '0' && c <= '9'; }))
        {
            throw std::invalid_argument("hgs seed must be an unsigned 32-bit integer");
        }
        const auto seed = std::stoull(text);
        if (seed > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("hgs seed must be an unsigned 32-bit integer");
        }
        return static_cast<std::uint32_t>(seed);
    }

    const char *stopReasonName(router::HgsStopReason reason)
    {
        switch (reason)
        {
        case router::HgsStopReason::IterationLimit: return "iteration_limit";
        case router::HgsStopReason::StagnationLimit: return "stagnation_limit";
        case router::HgsStopReason::TimeLimit: return "time_limit";
        case router::HgsStopReason::EmptyInstance: return "empty_instance";
        }
        throw std::logic_error("unknown HGS stop reason");
    }

    nlohmann::json penaltyJson(const router::HgsPenaltyWeights &weights)
    {
        return {
            {"weight", weights.weightPenalty},
            {"volume", weights.volumePenalty},
            {"time_warp", weights.timeWarpPenalty}};
    }
}

int main(int argc, char **argv)
{
    nlohmann::json runResult;
    bool solverStarted = false;
    std::chrono::steady_clock::time_point solverStart;

    try
    {
        router::HgsRunOptions options;
        bool seedSpecified = false;
        std::vector<std::string> positionalArgs;
        for (int index = 1; index < argc; ++index)
        {
            const std::string arg = argv[index];
            if (arg == "--seed")
            {
                if (seedSpecified || index + 1 >= argc)
                {
                    throw std::invalid_argument("--seed requires one value and cannot be repeated");
                }
                options.seed = parseHgsSeed(argv[++index]);
                seedSpecified = true;
            }
            else if (arg.starts_with("--"))
            {
                throw std::invalid_argument("unknown option: " + arg);
            }
            else
            {
                positionalArgs.push_back(arg);
            }
        }
        if (positionalArgs.empty() ||
            (positionalArgs.size() != 1 && positionalArgs.size() != 2 &&
             positionalArgs.size() != 4))
        {
            std::cerr << "usage: run_hgs <instance.json> "
                         "[time_limit_seconds [num_zones|auto zone_penalty]] [--seed N]\n";
            return 2;
        }
        double timeLimitSeconds = 1.0;
        if (positionalArgs.size() >= 2)
        {
            std::size_t parsedChars = 0;
            timeLimitSeconds = std::stod(positionalArgs[1], &parsedChars);
            if (parsedChars != positionalArgs[1].size() ||
                !std::isfinite(timeLimitSeconds) ||
                timeLimitSeconds < 1e-9 || timeLimitSeconds > 86400.0)
            {
                throw std::invalid_argument("time_limit_seconds must be between 1e-9 and 86400");
            }
        }
        const std::int64_t timeLimitNs = std::llround(timeLimitSeconds * 1e9);
        const double timeLimitMs = static_cast<double>(timeLimitNs) / 1e6;
        const router::Instance inst = router::loadInstance(positionalArgs[0]);

        const bool zoningEnabled = positionalArgs.size() == 4;
        bool automaticZones = false;
        int numZones = 0;
        int candidateMaxZones = 0;
        double silhouetteScore = 0.0;
        double zonePenalty = 0.0;
        if (zoningEnabled)
        {
            std::size_t parsedChars = 0;
            const std::string numZonesArg = positionalArgs[2];
            if (numZonesArg == "auto")
            {
                automaticZones = true;
            }
            else
            {
                numZones = std::stoi(numZonesArg, &parsedChars);
                if (parsedChars != numZonesArg.size() ||
                    numZones < 1 || numZones > inst.n)
                {
                    throw std::invalid_argument(
                        "num_zones must be 'auto' or between 1 "
                        "and the customer count");
                }
            }

            parsedChars = 0;
            const std::string zonePenaltyArg = positionalArgs[3];
            zonePenalty = std::stod(zonePenaltyArg, &parsedChars);
            if (parsedChars != zonePenaltyArg.size() ||
                !std::isfinite(zonePenalty) ||
                zonePenalty < 0.0)
            {
                throw std::invalid_argument(
                    "zone_penalty must be finite and non-negative");
            }
        }

        std::vector<int> zoneOf;
        double zonePreprocessingMs = 0.0;
        if (zoningEnabled)
        {
            const auto zoneStart = std::chrono::steady_clock::now();
            if (automaticZones)
            {
                router::ZoneSelection selection =
                    router::selectZones(inst);
                numZones = selection.numZones;
                candidateMaxZones =
                    selection.candidateMaxZones;
                silhouetteScore =
                    selection.silhouetteScore;
                zoneOf = std::move(selection.zoneOf);
            }
            else
            {
                zoneOf = router::assignZones(inst, numZones);
            }
            const auto zoneEnd = std::chrono::steady_clock::now();
            zonePreprocessingMs =
                std::chrono::duration<double, std::milli>(
                    zoneEnd - zoneStart).count();
        }

        std::ifstream instanceFile(positionalArgs[0]);
        nlohmann::json instanceJson;
        instanceFile >> instanceJson;
        const auto &meta = instanceJson.at("meta");

        runResult = {
            {"schema_version", 1},
            {"instance", positionalArgs[0]},
            {"spatial_class", meta.value("spatial_class", "")},
            {"demand_class", meta.value("demand_class", "")},
            {"size", inst.n},
            {"seed", inst.seed},
            {"solver", "hgs"},
            {"mode", "SPLIT"},
            {"num_zones", numZones},
            {"zone_count_policy",
             zoningEnabled
                 ? nlohmann::json(
                       automaticZones
                           ? "silhouette_2sqrt_n"
                           : "explicit")
                 : nlohmann::json(nullptr)},
            {"zone_candidate_max",
             automaticZones
                 ? nlohmann::json(candidateMaxZones)
                 : nlohmann::json(nullptr)},
            {"zone_silhouette_score",
             automaticZones
                 ? nlohmann::json(silhouetteScore)
                 : nlohmann::json(nullptr)},
            {"zone_penalty", zoningEnabled
                                 ? nlohmann::json(zonePenalty)
                                 : nlohmann::json(nullptr)},
            {"zone_objective", zoningEnabled
                                  ? nlohmann::json("route_zone_excess")
                                  : nlohmann::json(nullptr)},
            {"zone_preprocessing_ms", zoningEnabled
                                         ? nlohmann::json(zonePreprocessingMs)
                                         : nlohmann::json(nullptr)},
            {"solved", false},
            {"valid", nullptr},
            {"distance_cost", nullptr},
            {"route_count", nullptr},
            {"reference_cost", nullptr},
            {"reference_gap_pct", nullptr},
            {"zone_fragmentation", nullptr},
            {"route_zone_excess", nullptr},
            {"avg_zones_per_route", nullptr},
            {"route_zone_cost", nullptr},
            {"search_score", nullptr},
            {"runtime_ms", nullptr},
            {"runtime_scope", "hgs_setup_plus_construction_plus_search"},
            {"time_budget_ms", timeLimitMs},
            {"hgs_seed", options.seed},
            {"hgs_parameters", {
                {"mu", options.mu}, {"lambda", options.lambda},
                {"n_close", options.nClose}, {"n_elite", options.nElite},
                {"max_iterations", options.maxIterations},
                {"max_non_improving_iterations", options.maxNonImprovingIterations},
                {"diversification_interval", options.diversificationInterval},
                {"max_accepted_education_moves", options.maxAcceptedEducationMoves},
                {"repair_probability", options.repairProbability},
                {"penalty_control", {
                    {"window_size", options.penaltyControl.windowSize},
                    {"target_feasible", options.penaltyControl.targetFeasible},
                    {"tolerance", options.penaltyControl.tolerance},
                    {"increase_factor", options.penaltyControl.increaseFactor},
                    {"decrease_factor", options.penaltyControl.decreaseFactor},
                    {"min_penalty", options.penaltyControl.minPenalty},
                    {"max_penalty", options.penaltyControl.maxPenalty}}}}},
            {"hgs_preprocessing_ms", nullptr},
            {"construction_ms", nullptr},
            {"seed_validation_ms", nullptr},
            {"hgs_runtime_ms", nullptr},
            {"budget_exceeded_ms", nullptr},
            {"seed_solver", "solomon_i1"},
            {"i1_seed_route_count", nullptr},
            {"seed_fleet_repair_ms", 0.0},
            {"seed_route_count", nullptr},
            {"seed_valid", nullptr},
            {"seed_ready_within_budget", nullptr},
            {"within_time_budget", nullptr},
            {"seed_validation_violations", nullptr},
            {"seed_distance_cost", nullptr},
            {"seed_objective_cost", nullptr},
            {"objective_cost", nullptr},
            {"hgs_initial_penalties", nullptr},
            {"hgs_final_penalties", nullptr},
            {"iterations", nullptr},
            {"random_tour_attempts", nullptr},
            {"split_failures", nullptr},
            {"repairs_attempted", nullptr},
            {"repairs_succeeded", nullptr},
            {"penalty_updates", nullptr},
            {"diversifications", nullptr},
            {"stop_reason", nullptr},
            {"status", nullptr},
            {"error", nullptr}
        };

        solverStart = Clock::now();
        solverStarted = true;
        const auto budget = std::chrono::duration_cast<Clock::duration>(
            std::chrono::nanoseconds(timeLimitNs));
        if (budget <= Clock::duration::zero() ||
            budget > Clock::time_point::max() - solverStart)
        {
            throw std::invalid_argument("time budget cannot be represented by steady_clock");
        }
        options.deadline = solverStart + budget;
        const auto visitCatalog = router::splitCustomers(inst);
        const auto initialPenalties =
            router::makeInitialHgsPenalties(inst, visitCatalog, options.penaltyControl);
        const auto setupEnd = Clock::now();
        runResult["hgs_preprocessing_ms"] = elapsedMs(solverStart, setupEnd);
        runResult["hgs_initial_penalties"] = penaltyJson(initialPenalties);

        router::Solution seed;
        const auto constructionStart = Clock::now();
        try
        {
            seed = router::solomonI1(inst, false);
            const std::size_t i1RouteCount = seed.routes.size();
            runResult["i1_seed_route_count"] = i1RouteCount;

            if (seed.routes.size() > static_cast<std::size_t>(inst.fleet.size) &&
                (!options.deadline.has_value() || Clock::now() < *options.deadline))
            {
                const auto repairStart = Clock::now();
                seed = router::localSearch(inst, std::move(seed));
                const auto repairEnd = Clock::now();
                const double repairMs =
                    std::chrono::duration<double, std::milli>(repairEnd - repairStart).count();
                runResult["seed_solver"] = "solomon_i1_plus_fleet_repair_ls";
                runResult["seed_fleet_repair_ms"] = repairMs;
            }
        }
        catch (const std::runtime_error &error)
        {
            const auto end = Clock::now();
            runResult["construction_ms"] = elapsedMs(constructionStart, end);
            runResult["runtime_ms"] = elapsedMs(solverStart, end);
            runResult["budget_exceeded_ms"] =
                std::max(0.0, runResult["runtime_ms"].get<double>() - timeLimitMs);
            runResult["within_time_budget"] =
                runResult["runtime_ms"].get<double>() <= timeLimitMs;
            runResult["seed_valid"] = false;
            runResult["seed_ready_within_budget"] = false;
            runResult["stop_reason"] = "seed_construction_failed";
            runResult["error"] = error.what();
            std::cout << "RESULT_JSON " << runResult.dump() << "\n";
            std::cerr << "ERROR: " << error.what() << "\n";
            return 1;
        }
        const auto constructionEnd = Clock::now();
        const auto seedReport = router::validate(inst, seed);
        const auto seedValidationEnd = Clock::now();
        runResult["construction_ms"] = elapsedMs(constructionStart, constructionEnd);
        runResult["seed_validation_ms"] = elapsedMs(constructionEnd, seedValidationEnd);
        runResult["seed_route_count"] = seed.routes.size();
        runResult["seed_valid"] = seedReport.feasible;
        runResult["seed_ready_within_budget"] =
            seedReport.feasible && seedValidationEnd <= *options.deadline;
        runResult["seed_validation_violations"] = seedReport.violations;
        if (!seedReport.feasible)
        {
            runResult["runtime_ms"] = elapsedMs(solverStart, seedValidationEnd);
            runResult["budget_exceeded_ms"] =
                std::max(0.0, runResult["runtime_ms"].get<double>() - timeLimitMs);
            runResult["within_time_budget"] =
                runResult["runtime_ms"].get<double>() <= timeLimitMs;
            runResult["stop_reason"] = "seed_construction_failed";
            runResult["error"] = "Solomon I1 did not produce a valid seed within the available fleet";
            std::cout << "RESULT_JSON " << runResult.dump() << "\n";
            std::cerr << "ERROR: " << runResult["error"].get<std::string>() << "\n";
            return 1;
        }

        const auto hgsStart = Clock::now();
        const auto hgs = zoningEnabled
            ? router::runHgs(inst, visitCatalog, seed, initialPenalties, options, zoneOf, zonePenalty)
            : router::runHgs(inst, visitCatalog, seed, initialPenalties, options);
        const auto solverEnd = Clock::now();
        const router::Solution &sol = hgs.bestFeasible.decodedSolution;
        const double runtimeMs = elapsedMs(solverStart, solverEnd);
        runResult["solved"] = true;
        runResult["runtime_ms"] = runtimeMs;
        runResult["hgs_runtime_ms"] = elapsedMs(hgsStart, solverEnd);
        runResult["within_time_budget"] = solverEnd <= *options.deadline;
        runResult["budget_exceeded_ms"] = std::max(0.0, runtimeMs - timeLimitMs);
        runResult["hgs_final_penalties"] = penaltyJson(hgs.finalPenaltyWeights);
        runResult["objective_cost"] = hgs.bestFeasible.evaluation.objectiveCost;
        runResult["iterations"] = hgs.iterations;
        runResult["random_tour_attempts"] = hgs.randomTourAttempts;
        runResult["split_failures"] = hgs.splitFailures;
        runResult["repairs_attempted"] = hgs.repairsAttempted;
        runResult["repairs_succeeded"] = hgs.repairsSucceeded;
        runResult["penalty_updates"] = hgs.penaltyUpdates;
        runResult["diversifications"] = hgs.diversifications;
        runResult["stop_reason"] = stopReasonName(hgs.stopReason);

        const router::ValidationReport report = router::validate(inst, sol);
        const double seedCost = router::computeCost(inst, seed);
        const double cost = router::computeCost(inst, sol);
        const double seedZoneCost = zoningEnabled
            ? router::computeRouteZoneCost(seed, zoneOf, zonePenalty) : 0.0;
        runResult["seed_distance_cost"] = seedCost;
        runResult["seed_objective_cost"] = seedCost + seedZoneCost;

        runResult["valid"] = report.feasible;
        runResult["distance_cost"] = cost;
        runResult["route_count"] = sol.routes.size();

        std::cout << "n              = " << inst.n << "\n";
        std::cout << "routes used    = " << sol.routes.size()
                  << " / " << inst.fleet.size << "\n";

        const std::size_t chunks = visitCatalog.size();
        std::cout << "chunks         = " << chunks << "\n";
        std::cout << "chunks/route   = "
                  << (sol.routes.empty()
                          ? 0.0
                          : static_cast<double>(chunks) /
                                static_cast<double>(sol.routes.size()))
                  << "\n";
        std::cout << "num_zones      = " << numZones << "\n";
        if (zoningEnabled)
        {
            const router::ZoneMetrics zoneMetrics =
                router::measureZoneCoherence(sol, zoneOf);
            const double routeZoneCost =
                router::computeRouteZoneCost(
                    sol, zoneOf, zonePenalty);

            runResult["zone_fragmentation"] =
                zoneMetrics.fragmentation;
            runResult["route_zone_excess"] =
                zoneMetrics.routeZoneExcess;
            runResult["avg_zones_per_route"] =
                zoneMetrics.averageZonesPerRoute;
            runResult["route_zone_cost"] = routeZoneCost;
            runResult["search_score"] = cost + routeZoneCost;

            std::cout << "zone policy    = "
                      << (automaticZones
                              ? "silhouette_2sqrt_n"
                              : "explicit")
                      << "\n";
            if (automaticZones)
            {
                std::cout << "candidate max  = "
                          << candidateMaxZones << "\n";
                std::cout << "silhouette     = "
                          << silhouetteScore << "\n";
            }
            std::cout << "zone_penalty   = " << zonePenalty << "\n";
            std::cout << "zone prep time = "
                      << zonePreprocessingMs << " ms\n";
            std::cout << "zone fragment. = "
                      << zoneMetrics.fragmentation << "\n";
            std::cout << "route zone excess= "
                      << zoneMetrics.routeZoneExcess << "\n";
            std::cout << "avg zones/route= "
                      << zoneMetrics.averageZonesPerRoute << "\n";
            std::cout << "route zone cost= "
                      << routeZoneCost << "\n";
            std::cout << "search score   = "
                      << (cost + routeZoneCost) << "\n";
        }

        std::cout << "valid          = " << (report.feasible ? "YES" : "NO") << "\n";
        if (!report.feasible)
        {
            for (const auto &v : report.violations)
            {
                std::cout << "    - " << v << "\n";
            }
        }
        std::cout << "seed solver    = "
                  << runResult["seed_solver"].get<std::string>() << "\n";
        if (runResult["seed_solver"].get<std::string>() ==
            "solomon_i1_plus_fleet_repair_ls")
        {
            std::cout << "seed repair    = "
                      << runResult["i1_seed_route_count"].get<std::size_t>()
                      << " -> " << seed.routes.size() << " routes ("
                      << runResult["seed_fleet_repair_ms"].get<double>()
                      << " ms)\n";
        }
        std::cout << "routes (seed)  = " << seed.routes.size() << " / "
                  << inst.fleet.size << "\n";
        std::cout << "cost (seed)    = " << seedCost << "\n";
        std::cout << "cost (HGS)     = " << cost << "\n";
        if (std::isfinite(seedCost) && seedCost > 0.0)
        {
            std::cout << "distance impr. = "
                      << ((seedCost - cost) / seedCost * 100.0) << " %\n";
        }

        if (meta.contains("difficulty") &&
            meta["difficulty"].contains("reference_cost") &&
            meta["difficulty"]["reference_cost"].is_number())
        {
            const double referenceCost =
                meta["difficulty"]["reference_cost"].get<double>();
            if (std::isfinite(referenceCost) && referenceCost > 0.0)
            {
                const double gapPct =
                    (cost - referenceCost) / referenceCost * 100.0;
                runResult["reference_cost"] = referenceCost;
                runResult["reference_gap_pct"] = gapPct;
                std::cout << "reference_cost = " << referenceCost << "\n";
                std::cout << "vs reference   = " << gapPct << " %\n";
            }
        }
        std::cout << "seed objective = " << runResult["seed_objective_cost"] << "\n";
        std::cout << "hgs objective  = " << runResult["objective_cost"] << "\n";
        std::cout << "hgs seed       = " << options.seed << "\n";
        std::cout << "stop reason    = " << stopReasonName(hgs.stopReason) << "\n";
        std::cout << "iterations     = " << hgs.iterations << "\n";
        std::cout << "random tours   = " << hgs.randomTourAttempts << "\n";
        std::cout << "repairs        = " << hgs.repairsSucceeded
                  << " / " << hgs.repairsAttempted << "\n";
        std::cout << "hgs prep time  = " << runResult["hgs_preprocessing_ms"] << " ms\n";
        std::cout << "construct time = " << runResult["construction_ms"] << " ms\n";
        std::cout << "seed check time= " << runResult["seed_validation_ms"] << " ms\n";
        std::cout << "hgs time       = " << runResult["hgs_runtime_ms"] << " ms\n";
        std::cout << "total time     = " << runtimeMs << " ms\n";
        std::cout << "time budget    = " << timeLimitMs << " ms\n";
        std::cout << "seed in budget = "
                  << (runResult["seed_ready_within_budget"].get<bool>() ? "YES" : "NO") << "\n";
        std::cout << "within budget  = "
                  << (runResult["within_time_budget"].get<bool>() ? "YES" : "NO") << "\n";
        std::cout << "over budget    = " << runResult["budget_exceeded_ms"] << " ms\n";
        std::cout << "RESULT_JSON " << runResult.dump() << "\n";

        return report.feasible ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        if (solverStarted)
        {
            if (runResult["runtime_ms"].is_null())
            {
                runResult["runtime_ms"] =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - solverStart).count();
            }
            runResult["budget_exceeded_ms"] = std::max(
                0.0, runResult["runtime_ms"].get<double>() -
                    runResult["time_budget_ms"].get<double>());
            runResult["within_time_budget"] =
                runResult["runtime_ms"].get<double>() <=
                runResult["time_budget_ms"].get<double>();
            runResult["stop_reason"] = "error";
            runResult["error"] = e.what();
            std::cout << "RESULT_JSON " << runResult.dump() << "\n";
        }
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
