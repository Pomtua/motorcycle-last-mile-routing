#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "router/cost.hpp"
#include "router/hgs/genetic.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/search_context.hpp"
#include "router/instance_io.hpp"
#include "router/local_search.hpp"
#include "router/solomon_i1.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"
#include "router/zones.hpp"

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::size_t kGranularity = 20;
    constexpr double kReserveShare = 0.01;
    constexpr double kMaxReserveMs = 100.0;

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

    nlohmann::json penaltyJson(const router::hgs::Penalties &penalties)
    {
        return {{"weight", penalties.weight}, {"volume", penalties.volume}, {"time_warp", penalties.timeWarp}};
    }

    nlohmann::json stageJson(const router::hgs::StageStats &stats)
    {
        return {{"calls", stats.calls}, {"total_ms", stats.totalMs}};
    }

    nlohmann::json statsJson(const router::hgs::GeneticResult &result)
    {
        const auto &stats = result.stats;
        nlohmann::json moves = nlohmann::json::object();
        for (const auto &[name, move] : result.moves)
        {
            moves[name] = {
                {"evaluated", move.evaluated},
                {"improving", move.improving},
                {"rejected_by_conflict", move.rejectedByConflict},
                {"applied", move.applied}};
        }
        return {
            {"iteration_of_best", stats.iterationOfBest},
            {"ms_to_best", stats.msToBest},
            {"stages", {
                {"fill", stageJson(stats.fill)},
                {"parent_selection", stageJson(stats.parentSelection)},
                {"crossover", stageJson(stats.crossover)},
                {"split", stageJson(stats.split)},
                {"education", stageJson(stats.education)},
                {"repair", stageJson(stats.repair)},
                {"population_update", stageJson(stats.populationUpdate)}}},
            {"local_search", {
                {"runs", result.localSearch.runs},
                {"passes", result.localSearch.passes},
                {"pairs_skipped", result.localSearch.pairsSkipped},
                {"interrupted_by_deadline", result.localSearch.interruptedByDeadline},
                {"total_ms", result.localSearch.time.totalMs}}},
            {"moves", moves},
            {"population", {
                {"added", result.population.added},
                {"distances_computed", result.population.distancesComputed},
                {"survivor_selections", result.population.survivorSelections},
                {"removed_clones", result.population.removedClones},
                {"removed_by_fitness", result.population.removedByFitness}}}};
    }

    nlohmann::json configJson(const router::hgs::GeneticConfig &config)
    {
        return {
            {"mu", config.population.mu},
            {"lambda", config.population.lambda},
            {"n_close", config.population.nClose},
            {"n_elite", config.population.nElite},
            {"granularity", kGranularity},
            {"moves", config.moves},
            {"max_iterations", config.maxIterations},
            {"max_non_improving_iterations", config.maxNonImprovingIterations},
            {"diversification_interval", config.diversificationInterval},
            {"repair_probability", config.repairProbability},
            {"repair_penalty_factor", config.repairPenaltyFactor},
            {"perturbed_fill_fraction", config.perturbedFillFraction},
            {"perturbation_strength", config.perturbationStrength},
            {"fill_time_fraction", config.fillTimeFraction},
            {"check_invariants", config.checkInvariants},
            {"trace_interval", config.traceInterval},
            {"penalty_control", {
                {"window_size", config.penaltyControl.windowSize},
                {"target_feasible", config.penaltyControl.targetFeasible},
                {"tolerance", config.penaltyControl.tolerance},
                {"increase_factor", config.penaltyControl.increaseFactor},
                {"decrease_factor", config.penaltyControl.decreaseFactor},
                {"min_penalty", config.penaltyControl.minPenalty},
                {"max_penalty", config.penaltyControl.maxPenalty}}}};
    }
}

int main(int argc, char **argv)
{
    nlohmann::json runResult;
    bool solverStarted = false;
    Clock::time_point solverStart;

    try
    {
        router::hgs::GeneticConfig config;
        bool seedSpecified = false;
        std::optional<std::string> tracePath;
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
                config.seed = parseHgsSeed(argv[++index]);
                seedSpecified = true;
            }
            else if (arg == "--trace")
            {
                if (tracePath || index + 1 >= argc)
                {
                    throw std::invalid_argument("--trace requires one path and cannot be repeated");
                }
                tracePath = argv[++index];
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
            (positionalArgs.size() != 1 && positionalArgs.size() != 2 && positionalArgs.size() != 4))
        {
            std::cerr << "usage: run_hgs <instance.json> "
                         "[time_limit_seconds [num_zones|auto zone_penalty]] [--seed N] [--trace file.jsonl]\n";
            return 2;
        }
        const char *checkFlag = std::getenv("HGS_CHECK");
        config.checkInvariants = checkFlag != nullptr && std::string(checkFlag) != "0";
        config.traceInterval = tracePath ? 100 : 0;

        double timeLimitSeconds = 1.0;
        if (positionalArgs.size() >= 2)
        {
            std::size_t parsedChars = 0;
            timeLimitSeconds = std::stod(positionalArgs[1], &parsedChars);
            if (parsedChars != positionalArgs[1].size() || !std::isfinite(timeLimitSeconds) ||
                timeLimitSeconds < 1e-9 || timeLimitSeconds > 86400.0)
            {
                throw std::invalid_argument("time_limit_seconds must be between 1e-9 and 86400");
            }
        }
        const std::int64_t timeLimitNs = std::llround(timeLimitSeconds * 1e9);
        const double timeLimitMs = static_cast<double>(timeLimitNs) / 1e6;
        const double reserveMs = std::min(kMaxReserveMs, timeLimitMs * kReserveShare);
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
                if (parsedChars != numZonesArg.size() || numZones < 1 || numZones > inst.n)
                {
                    throw std::invalid_argument("num_zones must be 'auto' or between 1 and the customer count");
                }
            }

            parsedChars = 0;
            const std::string zonePenaltyArg = positionalArgs[3];
            zonePenalty = std::stod(zonePenaltyArg, &parsedChars);
            if (parsedChars != zonePenaltyArg.size() || !std::isfinite(zonePenalty) || zonePenalty < 0.0)
            {
                throw std::invalid_argument("zone_penalty must be finite and non-negative");
            }
        }

        std::vector<int> zoneOf;
        double zonePreprocessingMs = 0.0;
        if (zoningEnabled)
        {
            const auto zoneStart = Clock::now();
            if (automaticZones)
            {
                router::ZoneSelection selection = router::selectZones(inst);
                numZones = selection.numZones;
                candidateMaxZones = selection.candidateMaxZones;
                silhouetteScore = selection.silhouetteScore;
                zoneOf = std::move(selection.zoneOf);
            }
            else
            {
                zoneOf = router::assignZones(inst, numZones);
            }
            zonePreprocessingMs = elapsedMs(zoneStart, Clock::now());
        }

        std::ifstream instanceFile(positionalArgs[0]);
        nlohmann::json instanceJson;
        instanceFile >> instanceJson;
        const auto &meta = instanceJson.at("meta");
        const auto optionalNumber = [](bool present, auto value)
        { return present ? nlohmann::json(value) : nlohmann::json(nullptr); };

        runResult = {
            {"schema_version", 1},
            {"instance", positionalArgs[0]},
            {"spatial_class", meta.value("spatial_class", "")},
            {"demand_class", meta.value("demand_class", "")},
            {"size", inst.n},
            {"seed", inst.seed},
            {"solver", "hgs"},
            {"hgs_version", 2},
            {"mode", "SPLIT"},
            {"num_zones", numZones},
            {"zone_count_policy", zoningEnabled
                                      ? nlohmann::json(automaticZones ? "silhouette_2sqrt_n" : "explicit")
                                      : nlohmann::json(nullptr)},
            {"zone_candidate_max", optionalNumber(automaticZones, candidateMaxZones)},
            {"zone_silhouette_score", optionalNumber(automaticZones, silhouetteScore)},
            {"zone_penalty", optionalNumber(zoningEnabled, zonePenalty)},
            {"zone_objective", zoningEnabled ? nlohmann::json("route_zone_excess") : nlohmann::json(nullptr)},
            {"zone_preprocessing_ms", optionalNumber(zoningEnabled, zonePreprocessingMs)},
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
            {"search_deadline_reserve_ms", reserveMs},
            {"hgs_seed", config.seed},
            {"hgs_parameters", configJson(config)},
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
            {"perturbed_fill_attempts", nullptr},
            {"fills_cut_by_time", nullptr},
            {"split_failures", nullptr},
            {"repairs_attempted", nullptr},
            {"repairs_succeeded", nullptr},
            {"penalty_updates", nullptr},
            {"diversifications", nullptr},
            {"hgs_stats", nullptr},
            {"stop_reason", nullptr},
            {"status", nullptr},
            {"error", nullptr}};

        solverStart = Clock::now();
        solverStarted = true;
        const auto budget = std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(timeLimitNs));
        if (budget <= Clock::duration::zero() || budget > Clock::time_point::max() - solverStart)
        {
            throw std::invalid_argument("time budget cannot be represented by steady_clock");
        }
        const Clock::time_point budgetEnd = solverStart + budget;
        const Clock::time_point searchEnd =
            budgetEnd - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(reserveMs));

        const auto visitCatalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, visitCatalog, {zoneOf, zonePenalty, kGranularity});
        const auto setupEnd = Clock::now();
        runResult["hgs_preprocessing_ms"] = elapsedMs(solverStart, setupEnd);

        router::Solution seed;
        const auto constructionStart = Clock::now();
        try
        {
            seed = router::solomonI1(inst, false);
            runResult["i1_seed_route_count"] = seed.routes.size();
            if (seed.routes.size() > static_cast<std::size_t>(inst.fleet.size) && Clock::now() < searchEnd)
            {
                const auto repairStart = Clock::now();
                seed = router::localSearch(inst, std::move(seed));
                runResult["seed_solver"] = "solomon_i1_plus_fleet_repair_ls";
                runResult["seed_fleet_repair_ms"] = elapsedMs(repairStart, Clock::now());
            }
        }
        catch (const std::runtime_error &error)
        {
            const auto end = Clock::now();
            runResult["construction_ms"] = elapsedMs(constructionStart, end);
            runResult["runtime_ms"] = elapsedMs(solverStart, end);
            runResult["budget_exceeded_ms"] = std::max(0.0, runResult["runtime_ms"].get<double>() - timeLimitMs);
            runResult["within_time_budget"] = runResult["runtime_ms"].get<double>() <= timeLimitMs;
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
        runResult["seed_ready_within_budget"] = seedReport.feasible && seedValidationEnd <= budgetEnd;
        runResult["seed_validation_violations"] = seedReport.violations;
        if (!seedReport.feasible)
        {
            runResult["runtime_ms"] = elapsedMs(solverStart, seedValidationEnd);
            runResult["budget_exceeded_ms"] = std::max(0.0, runResult["runtime_ms"].get<double>() - timeLimitMs);
            runResult["within_time_budget"] = runResult["runtime_ms"].get<double>() <= timeLimitMs;
            runResult["stop_reason"] = "seed_construction_failed";
            runResult["error"] = "Solomon I1 did not produce a valid seed within the available fleet";
            std::cout << "RESULT_JSON " << runResult.dump() << "\n";
            std::cerr << "ERROR: " << runResult["error"].get<std::string>() << "\n";
            return 1;
        }

        std::ofstream traceFile;
        router::hgs::TraceSink trace;
        if (tracePath)
        {
            traceFile.open(*tracePath);
            if (!traceFile)
            {
                throw std::runtime_error("cannot open trace file: " + *tracePath);
            }
            trace = [&traceFile](const router::hgs::TraceRecord &record)
            {
                traceFile << nlohmann::json{
                                 {"iteration", record.iteration},
                                 {"elapsed_ms", record.elapsedMs},
                                 {"best_objective", record.bestObjective ? nlohmann::json(*record.bestObjective) : nlohmann::json(nullptr)},
                                 {"feasible_size", record.feasibleSize},
                                 {"infeasible_size", record.infeasibleSize},
                                 {"penalties", penaltyJson(record.penalties)}}
                                 .dump()
                          << "\n";
            };
        }

        const router::hgs::Individual seedIndividual = router::hgs::individualFromSolution(data, seed);
        const auto hgsStart = Clock::now();
        const router::hgs::GeneticResult hgs =
            router::hgs::runGenetic(data, seedIndividual, config, router::hgs::Deadline(searchEnd), trace);
        const router::Solution sol = router::hgs::toSolution(data, hgs.best);
        const auto searchFinished = Clock::now();

        const router::ValidationReport report = router::validate(inst, sol);
        const double seedCost = router::computeCost(inst, seed);
        const double cost = router::computeCost(inst, sol);
        const double seedZoneCost = zoningEnabled ? router::computeRouteZoneCost(seed, zoneOf, zonePenalty) : 0.0;
        const auto solverEnd = Clock::now();
        const double runtimeMs = elapsedMs(solverStart, solverEnd);

        runResult["solved"] = true;
        runResult["runtime_ms"] = runtimeMs;
        runResult["hgs_runtime_ms"] = elapsedMs(hgsStart, searchFinished);
        runResult["within_time_budget"] = solverEnd <= budgetEnd;
        runResult["budget_exceeded_ms"] = std::max(0.0, runtimeMs - timeLimitMs);
        runResult["hgs_initial_penalties"] = penaltyJson(hgs.initialPenalties);
        runResult["hgs_final_penalties"] = penaltyJson(hgs.finalPenalties);
        runResult["objective_cost"] = router::hgs::objectiveCost(data, hgs.best.cost);
        runResult["iterations"] = hgs.stats.iterations;
        runResult["random_tour_attempts"] = hgs.stats.randomFillAttempts;
        runResult["perturbed_fill_attempts"] = hgs.stats.perturbedFillAttempts;
        runResult["fills_cut_by_time"] = hgs.stats.fillsCutByTime;
        runResult["split_failures"] = hgs.stats.splitFailures;
        runResult["repairs_attempted"] = hgs.stats.repairsAttempted;
        runResult["repairs_succeeded"] = hgs.stats.repairsSucceeded;
        runResult["penalty_updates"] = hgs.stats.penaltyUpdates;
        runResult["diversifications"] = hgs.stats.diversifications;
        runResult["hgs_stats"] = statsJson(hgs);
        runResult["stop_reason"] = router::hgs::stopReasonName(hgs.stopReason);
        runResult["seed_distance_cost"] = seedCost;
        runResult["seed_objective_cost"] = seedCost + seedZoneCost;
        runResult["valid"] = report.feasible;
        runResult["distance_cost"] = cost;
        runResult["route_count"] = sol.routes.size();

        std::cout << "n              = " << inst.n << "\n";
        std::cout << "routes used    = " << sol.routes.size() << " / " << inst.fleet.size << "\n";
        std::cout << "chunks         = " << visitCatalog.size() << "\n";
        std::cout << "chunks/route   = "
                  << (sol.routes.empty() ? 0.0 : static_cast<double>(visitCatalog.size()) / static_cast<double>(sol.routes.size()))
                  << "\n";
        std::cout << "num_zones      = " << numZones << "\n";
        if (zoningEnabled)
        {
            const router::ZoneMetrics zoneMetrics = router::measureZoneCoherence(sol, zoneOf);
            const double routeZoneCost = router::computeRouteZoneCost(sol, zoneOf, zonePenalty);
            runResult["zone_fragmentation"] = zoneMetrics.fragmentation;
            runResult["route_zone_excess"] = zoneMetrics.routeZoneExcess;
            runResult["avg_zones_per_route"] = zoneMetrics.averageZonesPerRoute;
            runResult["route_zone_cost"] = routeZoneCost;
            runResult["search_score"] = cost + routeZoneCost;

            std::cout << "zone policy    = " << (automaticZones ? "silhouette_2sqrt_n" : "explicit") << "\n";
            if (automaticZones)
            {
                std::cout << "candidate max  = " << candidateMaxZones << "\n";
                std::cout << "silhouette     = " << silhouetteScore << "\n";
            }
            std::cout << "zone_penalty   = " << zonePenalty << "\n";
            std::cout << "zone prep time = " << zonePreprocessingMs << " ms\n";
            std::cout << "zone fragment. = " << zoneMetrics.fragmentation << "\n";
            std::cout << "route zone excess= " << zoneMetrics.routeZoneExcess << "\n";
            std::cout << "avg zones/route= " << zoneMetrics.averageZonesPerRoute << "\n";
            std::cout << "route zone cost= " << routeZoneCost << "\n";
            std::cout << "search score   = " << (cost + routeZoneCost) << "\n";
        }

        std::cout << "valid          = " << (report.feasible ? "YES" : "NO") << "\n";
        for (const auto &violation : report.violations)
        {
            std::cout << "    - " << violation << "\n";
        }
        std::cout << "seed solver    = " << runResult["seed_solver"].get<std::string>() << "\n";
        if (runResult["seed_solver"].get<std::string>() == "solomon_i1_plus_fleet_repair_ls")
        {
            std::cout << "seed repair    = " << runResult["i1_seed_route_count"].get<std::size_t>() << " -> "
                      << seed.routes.size() << " routes (" << runResult["seed_fleet_repair_ms"].get<double>() << " ms)\n";
        }
        std::cout << "routes (seed)  = " << seed.routes.size() << " / " << inst.fleet.size << "\n";
        std::cout << "cost (seed)    = " << seedCost << "\n";
        std::cout << "cost (HGS)     = " << cost << "\n";
        if (std::isfinite(seedCost) && seedCost > 0.0)
        {
            std::cout << "distance impr. = " << ((seedCost - cost) / seedCost * 100.0) << " %\n";
        }
        if (meta.contains("difficulty") && meta["difficulty"].contains("reference_cost") &&
            meta["difficulty"]["reference_cost"].is_number())
        {
            const double referenceCost = meta["difficulty"]["reference_cost"].get<double>();
            if (std::isfinite(referenceCost) && referenceCost > 0.0)
            {
                const double gapPct = (cost - referenceCost) / referenceCost * 100.0;
                runResult["reference_cost"] = referenceCost;
                runResult["reference_gap_pct"] = gapPct;
                std::cout << "reference_cost = " << referenceCost << "\n";
                std::cout << "vs reference   = " << gapPct << " %\n";
            }
        }
        std::cout << "seed objective = " << runResult["seed_objective_cost"] << "\n";
        std::cout << "hgs objective  = " << runResult["objective_cost"] << "\n";
        std::cout << "hgs seed       = " << config.seed << "\n";
        std::cout << "stop reason    = " << router::hgs::stopReasonName(hgs.stopReason) << "\n";
        std::cout << "iterations     = " << hgs.stats.iterations << "\n";
        std::cout << "best found at  = iteration " << hgs.stats.iterationOfBest << ", "
                  << hgs.stats.msToBest << " ms\n";
        std::cout << "random tours   = " << hgs.stats.randomFillAttempts << "\n";
        std::cout << "perturbed fills= " << hgs.stats.perturbedFillAttempts << "\n";
        std::cout << "fills cut      = " << hgs.stats.fillsCutByTime << "\n";
        std::cout << "repairs        = " << hgs.stats.repairsSucceeded << " / " << hgs.stats.repairsAttempted << "\n";
        std::cout << "penalty updates= " << hgs.stats.penaltyUpdates << "\n";
        std::cout << "education      = " << hgs.stats.education.calls << " runs, " << hgs.stats.education.totalMs << " ms\n";
        std::cout << "hgs prep time  = " << runResult["hgs_preprocessing_ms"] << " ms\n";
        std::cout << "construct time = " << runResult["construction_ms"] << " ms\n";
        std::cout << "seed check time= " << runResult["seed_validation_ms"] << " ms\n";
        std::cout << "hgs time       = " << runResult["hgs_runtime_ms"] << " ms\n";
        std::cout << "total time     = " << runtimeMs << " ms\n";
        std::cout << "time budget    = " << timeLimitMs << " ms\n";
        std::cout << "seed in budget = " << (runResult["seed_ready_within_budget"].get<bool>() ? "YES" : "NO") << "\n";
        std::cout << "within budget  = " << (runResult["within_time_budget"].get<bool>() ? "YES" : "NO") << "\n";
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
                runResult["runtime_ms"] = elapsedMs(solverStart, Clock::now());
            }
            runResult["budget_exceeded_ms"] =
                std::max(0.0, runResult["runtime_ms"].get<double>() - runResult["time_budget_ms"].get<double>());
            runResult["within_time_budget"] =
                runResult["runtime_ms"].get<double>() <= runResult["time_budget_ms"].get<double>();
            runResult["stop_reason"] = "error";
            runResult["error"] = e.what();
            std::cout << "RESULT_JSON " << runResult.dump() << "\n";
        }
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
