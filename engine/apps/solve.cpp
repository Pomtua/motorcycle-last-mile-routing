#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
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
#include "router/nearest_neighbour.hpp"
#include "router/schedule.hpp"
#include "router/solomon_i1.hpp"
#include "router/split.hpp"
#include "router/validate.hpp"
#include "router/zones.hpp"

#ifdef ROUTER_WITH_ORTOOLS
#include "router/ortools_solver.hpp"
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::size_t kGranularity = 20;
    constexpr double kReserveShare = 0.01;
    constexpr double kMaxReserveMs = 100.0;
    constexpr double kCapacityTolerance = 1e-6;
    constexpr double kDefaultPenaltyAlpha = 0.25;

    enum class Algorithm
    {
        Hgs,
        I1Ls,
        Nn,
        OrTools
    };

    struct Zoning
    {
        bool enabled = false;
        std::optional<int> requestedZones;
        double penaltyAlpha = kDefaultPenaltyAlpha;
        int numZones = 0;
        double silhouetteScore = 0.0;
        std::int64_t penalty = 0;
        double preprocessingMs = 0.0;
        std::vector<int> zoneOf;
    };

    struct Outcome
    {
        router::Solution solution;
        double runtimeMs = 0.0;
        nlohmann::json details = nlohmann::json::object();
    };

    double elapsedMs(Clock::time_point start, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    Algorithm parseAlgorithm(const std::string &name)
    {
        if (name == "hgs")
        {
            return Algorithm::Hgs;
        }
        if (name == "i1_ls")
        {
            return Algorithm::I1Ls;
        }
        if (name == "nn")
        {
            return Algorithm::Nn;
        }
        if (name == "ortools")
        {
            return Algorithm::OrTools;
        }
        throw std::invalid_argument("algorithm must be hgs, i1_ls, nn or ortools");
    }

    double timeLimitFrom(const nlohmann::json &request)
    {
        if (!request.contains("time_limit_seconds"))
        {
            throw std::invalid_argument("time_limit_seconds is required for hgs and ortools");
        }
        const double seconds = request.at("time_limit_seconds").get<double>();
        if (!std::isfinite(seconds) || seconds < 1e-9 || seconds > 86400.0)
        {
            throw std::invalid_argument("time_limit_seconds must be between 1e-9 and 86400");
        }
        return seconds;
    }

    Zoning prepareZones(const router::Instance &inst, const nlohmann::json &request)
    {
        Zoning zoning;
        if (!request.contains("zoning") || request.at("zoning").is_null())
        {
            return zoning;
        }
        const auto &spec = request.at("zoning");
        zoning.enabled = spec.value("enabled", false);
        if (!zoning.enabled)
        {
            return zoning;
        }

        const auto &numZones = spec.contains("num_zones") ? spec.at("num_zones") : nlohmann::json("auto");
        if (!(numZones.is_string() && numZones.get<std::string>() == "auto"))
        {
            const int requested = numZones.get<int>();
            if (requested < 1 || requested > inst.n || requested > router::hgs::ProblemData::kMaxZones)
            {
                throw std::invalid_argument("num_zones must be 'auto' or between 1 and min(customer count, 64)");
            }
            zoning.requestedZones = requested;
        }
        zoning.penaltyAlpha = spec.value("penalty_alpha", kDefaultPenaltyAlpha);
        if (!std::isfinite(zoning.penaltyAlpha) || zoning.penaltyAlpha < 0.0)
        {
            throw std::invalid_argument("penalty_alpha must be finite and non-negative");
        }

        const auto start = Clock::now();
        if (zoning.requestedZones)
        {
            zoning.numZones = *zoning.requestedZones;
            zoning.zoneOf = router::assignZones(inst, zoning.numZones);
        }
        else
        {
            router::ZoneSelection selection = router::selectZones(inst);
            zoning.numZones = selection.numZones;
            zoning.silhouetteScore = selection.silhouetteScore;
            zoning.zoneOf = std::move(selection.zoneOf);
        }

        const router::Solution reference = router::localSearch(inst, router::solomonI1(inst, false));
        if (reference.routes.empty())
        {
            throw std::runtime_error("I1+LS produced no routes, so the zone penalty cannot be scaled");
        }
        const double distancePerRoute =
            router::computeCost(inst, reference) / static_cast<double>(reference.routes.size());
        zoning.penalty = static_cast<std::int64_t>(std::floor(zoning.penaltyAlpha * distancePerRoute + 0.5));
        zoning.preprocessingMs = elapsedMs(start, Clock::now());
        return zoning;
    }

    Outcome runNearestNeighbour(const router::Instance &inst)
    {
        Outcome outcome;
        const auto start = Clock::now();
        outcome.solution = router::nearestNeighbour(inst);
        outcome.runtimeMs = elapsedMs(start, Clock::now());
        outcome.details["runtime_scope"] = "nearest_neighbour";
        return outcome;
    }

    Outcome runI1LocalSearch(const router::Instance &inst, const Zoning &zoning)
    {
        Outcome outcome;
        const auto start = Clock::now();
        router::Solution seed = router::solomonI1(inst, false);
        outcome.solution = zoning.enabled
                               ? router::localSearch(inst, std::move(seed), zoning.zoneOf, static_cast<double>(zoning.penalty))
                               : router::localSearch(inst, std::move(seed));
        outcome.runtimeMs = elapsedMs(start, Clock::now());
        outcome.details["runtime_scope"] = "construction_plus_local_search";
        return outcome;
    }

    Outcome runHgs(const router::Instance &inst, const nlohmann::json &request, const Zoning &zoning)
    {
        const double timeLimitSeconds = timeLimitFrom(request);
        router::hgs::GeneticConfig config;
        config.maxIterations = std::numeric_limits<std::size_t>::max();
        config.splitMode = router::hgs::parseSplitMode(request.value("split_mode", std::string("dynamic")));
        const std::int64_t seed = request.value("seed", std::int64_t{42});
        if (seed < 0 || seed > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("seed must be an unsigned 32-bit integer");
        }
        config.seed = static_cast<std::uint32_t>(seed);

        const std::int64_t timeLimitNs = std::llround(timeLimitSeconds * 1e9);
        const double timeLimitMs = static_cast<double>(timeLimitNs) / 1e6;
        const double reserveMs = std::min(kMaxReserveMs, timeLimitMs * kReserveShare);

        Outcome outcome;
        const auto start = Clock::now();
        const Clock::time_point budgetEnd =
            start + std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(timeLimitNs));
        const Clock::time_point searchEnd =
            budgetEnd - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(reserveMs));

        const auto visitCatalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(
            inst, visitCatalog, {zoning.zoneOf, static_cast<double>(zoning.penalty), kGranularity, 0.0});

        router::Solution seedSolution = router::solomonI1(inst, false);
        if (seedSolution.routes.size() > static_cast<std::size_t>(inst.fleet.size) && Clock::now() < searchEnd)
        {
            seedSolution = router::localSearch(inst, std::move(seedSolution));
        }
        const router::ValidationReport seedReport = router::validate(inst, seedSolution);
        if (!seedReport.feasible)
        {
            throw std::runtime_error(
                "no feasible starting solution fits the available riders: add riders, raise capacity "
                "or widen the shift");
        }

        const router::hgs::Individual seedIndividual = router::hgs::individualFromSolution(data, seedSolution);
        const router::hgs::GeneticResult result =
            router::hgs::runGenetic(data, seedIndividual, config, router::hgs::Deadline(searchEnd));
        outcome.solution = router::hgs::toSolution(data, result.best);
        outcome.runtimeMs = elapsedMs(start, Clock::now());
        outcome.details = {
            {"runtime_scope", "hgs_setup_plus_construction_plus_search"},
            {"time_budget_ms", timeLimitMs},
            {"split_mode", router::hgs::splitModeName(config.splitMode)},
            {"seed", config.seed},
            {"iterations", result.stats.iterations},
            {"iteration_of_best", result.stats.iterationOfBest},
            {"ms_to_best", result.stats.msToBest},
            {"stop_reason", router::hgs::stopReasonName(result.stopReason)},
            {"split_customers", result.splits.splitCustomers},
            {"deliveries", result.splits.deliveries}};
        return outcome;
    }

    Outcome runOrTools(const router::Instance &inst, const nlohmann::json &request, const Zoning &zoning)
    {
#ifdef ROUTER_WITH_ORTOOLS
        const double timeLimitSeconds = timeLimitFrom(request);
        if (zoning.enabled && zoning.numZones > 1 &&
            zoning.penalty > std::numeric_limits<std::int64_t>::max() / static_cast<std::int64_t>(zoning.numZones - 1))
        {
            throw std::invalid_argument("zone penalty is too large for the number of zones");
        }
        const router::OrToolsResult result = router::solveWithOrTools(
            inst, router::splitCustomers(inst), timeLimitSeconds, zoning.zoneOf, zoning.penalty);
        if (!result.solved)
        {
            throw std::runtime_error("OR-Tools found no solution within the time limit (status " +
                                     std::to_string(result.status) + ")");
        }
        Outcome outcome;
        outcome.solution = result.solution;
        outcome.runtimeMs = result.solveMs;
        outcome.details = {
            {"runtime_scope", "search_only"},
            {"time_budget_ms", timeLimitSeconds * 1000.0},
            {"status", result.status}};
        return outcome;
#else
        (void)inst;
        (void)request;
        (void)zoning;
        throw std::runtime_error("this engine was built without OR-Tools (configure with -DWITH_ORTOOLS=ON)");
#endif
    }

    nlohmann::json describe(const router::Instance &inst, const Outcome &outcome, const Zoning &zoning)
    {
        const router::ValidationReport report = router::validate(inst, outcome.solution);

        nlohmann::json routes = nlohmann::json::array();
        bool capacityOk = true;
        bool timeWindowOk = true;
        int onTimeStops = 0;
        int totalStops = 0;
        double totalDuration = 0.0;
        for (std::size_t r = 0; r < outcome.solution.routes.size(); ++r)
        {
            const router::Route &route = outcome.solution.routes[r];
            const router::RouteSchedule schedule = router::scheduleRoute(inst, route);
            nlohmann::json stops = nlohmann::json::array();
            for (std::size_t s = 0; s < route.stops.size(); ++s)
            {
                const router::Visit &visit = route.stops[s];
                const router::StopTiming &timing = schedule.stops[s];
                stops.push_back({
                    {"node", visit.nodeIndex},
                    {"weight", visit.weight},
                    {"volume", visit.volume},
                    {"arrival", timing.arrival},
                    {"start", timing.start},
                    {"departure", timing.departure},
                    {"on_time", timing.onTime},
                    {"zone", zoning.enabled ? nlohmann::json(zoning.zoneOf[static_cast<std::size_t>(visit.nodeIndex)])
                                            : nlohmann::json(nullptr)}});
                onTimeStops += timing.onTime ? 1 : 0;
                ++totalStops;
            }
            capacityOk = capacityOk &&
                         schedule.loadWeight <= inst.fleet.weightCapacity + kCapacityTolerance &&
                         schedule.loadVolume <= inst.fleet.volumeCapacity + kCapacityTolerance;
            timeWindowOk = timeWindowOk && schedule.withinHorizon &&
                           std::all_of(schedule.stops.begin(), schedule.stops.end(),
                                       [](const router::StopTiming &timing) { return timing.onTime; });
            totalDuration += schedule.returnTime;
            routes.push_back({
                {"vehicle", r},
                {"distance", schedule.distance},
                {"duration", schedule.returnTime},
                {"return_time", schedule.returnTime},
                {"load_weight", schedule.loadWeight},
                {"load_volume", schedule.loadVolume},
                {"stops", std::move(stops)}});
        }

        nlohmann::json metrics = outcome.details;
        metrics["runtime_ms"] = outcome.runtimeMs;
        metrics["total_distance"] = router::computeCost(inst, outcome.solution);
        metrics["total_duration"] = totalDuration;
        metrics["vehicles_used"] = outcome.solution.routes.size();
        metrics["fleet_size"] = inst.fleet.size;

        nlohmann::json zones = nullptr;
        if (zoning.enabled)
        {
            const router::ZoneMetrics zoneMetrics = router::measureZoneCoherence(outcome.solution, zoning.zoneOf);
            metrics["route_zone_excess"] = zoneMetrics.routeZoneExcess;
            metrics["zone_fragmentation"] = zoneMetrics.fragmentation;
            metrics["avg_zones_per_route"] = zoneMetrics.averageZonesPerRoute;
            zones = {
                {"count", zoning.numZones},
                {"policy", zoning.requestedZones ? "explicit" : "silhouette_2sqrt_n"},
                {"silhouette_score", zoning.requestedZones ? nlohmann::json(nullptr) : nlohmann::json(zoning.silhouetteScore)},
                {"penalty_alpha", zoning.penaltyAlpha},
                {"penalty", zoning.penalty},
                {"preprocessing_ms", zoning.preprocessingMs},
                {"zone_of", zoning.zoneOf}};
        }

        return {
            {"status", "ok"},
            {"routes", std::move(routes)},
            {"zones", std::move(zones)},
            {"metrics", std::move(metrics)},
            {"validation", {
                {"feasible", report.feasible},
                {"capacity_ok", capacityOk},
                {"time_window_ok", timeWindowOk},
                {"on_time_stops", onTimeStops},
                {"total_stops", totalStops},
                {"violations", report.violations}}}};
    }
}

int main()
{
    try
    {
        std::ios::sync_with_stdio(false);
        const nlohmann::json request = nlohmann::json::parse(std::istreambuf_iterator<char>(std::cin),
                                                             std::istreambuf_iterator<char>());
        const Algorithm algorithm = parseAlgorithm(request.at("algorithm").get<std::string>());
        const router::Instance inst = router::instanceFromJson(request.at("instance"), "stdin");
        if (inst.n < 1)
        {
            throw std::invalid_argument("the instance has no customers");
        }
        const Zoning zoning = prepareZones(inst, request);

        Outcome outcome;
        switch (algorithm)
        {
        case Algorithm::Hgs:
            outcome = runHgs(inst, request, zoning);
            break;
        case Algorithm::I1Ls:
            outcome = runI1LocalSearch(inst, zoning);
            break;
        case Algorithm::Nn:
            outcome = runNearestNeighbour(inst);
            break;
        case Algorithm::OrTools:
            outcome = runOrTools(inst, request, zoning);
            break;
        }

        std::cout << describe(inst, outcome, zoning).dump() << "\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cout << nlohmann::json{{"status", "error"}, {"message", error.what()}}.dump() << "\n";
        return 1;
    }
}
