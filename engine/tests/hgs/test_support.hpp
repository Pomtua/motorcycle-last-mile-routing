#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "router/instance.hpp"
#include "router/split.hpp"

namespace hgs_test
{
    inline int failures = 0;

    inline void expect(bool condition, std::string_view message)
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

    inline bool near(double a, double b, double relative = 1e-9)
    {
        return std::abs(a - b) <= relative * std::max({1.0, std::abs(a), std::abs(b)});
    }

    inline int finish(std::string_view suite)
    {
        if (failures == 0)
        {
            std::cout << suite << ": all checks passed\n";
            return 0;
        }
        std::cerr << suite << ": " << failures << " check(s) failed\n";
        return 1;
    }

    struct RandomInstanceOptions
    {
        int customers = 30;
        int zones = 0;
        double weightCapacity = 30.0;
        double volumeCapacity = 0.1;
        double horizon = 28800.0;
        double maxDemandRatio = 1.8;
        double minWindow = 600.0;
        double maxWindow = 7200.0;
    };

    inline router::Instance makeRandomInstance(std::uint32_t seed, const RandomInstanceOptions &options = {})
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> coordinate(0.0, 10000.0);
        std::uniform_real_distribution<double> unit(0.0, 1.0);

        router::Instance inst;
        inst.seed = static_cast<int>(seed);
        inst.n = options.customers;
        inst.horizon = options.horizon;
        inst.fleet = {options.customers * 3, options.weightCapacity, options.volumeCapacity};

        std::vector<std::pair<double, double>> points;
        router::Node depot;
        depot.twStart = 0;
        depot.twEnd = static_cast<int>(options.horizon);
        inst.nodes.push_back(depot);
        points.emplace_back(5000.0, 5000.0);

        for (int customer = 1; customer <= options.customers; ++customer)
        {
            router::Node node;
            node.osmId = std::to_string(customer);
            node.demandWeight = std::round(unit(rng) * options.maxDemandRatio * options.weightCapacity * 100.0) / 100.0 + 0.5;
            node.demandVolume = std::round(unit(rng) * options.volumeCapacity * 5000.0) / 10000.0;
            node.serviceTime = 60 + static_cast<int>(unit(rng) * 240.0);
            const double width = options.minWindow + unit(rng) * (options.maxWindow - options.minWindow);
            const double start = unit(rng) * std::max(0.0, options.horizon - width);
            node.twStart = static_cast<int>(start);
            node.twEnd = static_cast<int>(start + width);
            inst.nodes.push_back(node);
            points.emplace_back(coordinate(rng), coordinate(rng));
        }

        const std::size_t count = inst.nodes.size();
        inst.distanceMatrix.assign(count, std::vector<double>(count, 0.0));
        inst.durationMatrix.assign(count, std::vector<double>(count, 0.0));
        for (std::size_t from = 0; from < count; ++from)
        {
            for (std::size_t to = 0; to < count; ++to)
            {
                if (from == to)
                {
                    continue;
                }
                const double dx = points[from].first - points[to].first;
                const double dy = points[from].second - points[to].second;
                const double distance = std::sqrt(dx * dx + dy * dy) * (1.0 + 0.3 * unit(rng));
                inst.distanceMatrix[from][to] = std::round(distance * 10.0) / 10.0;
                inst.durationMatrix[from][to] = std::round(distance / 8.0 * 10.0) / 10.0;
            }
        }
        return inst;
    }

    inline std::vector<int> makeRandomZones(std::uint32_t seed, const router::Instance &inst, int zones)
    {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> pick(0, zones - 1);
        std::vector<int> zoneOf(inst.nodes.size(), -1);
        for (std::size_t node = 1; node < zoneOf.size(); ++node)
        {
            zoneOf[node] = pick(rng);
        }
        return zoneOf;
    }

    inline std::vector<std::vector<int>> makeConflictFreeRoutes(
        std::mt19937 &rng,
        const std::vector<router::Visit> &catalog,
        double newRouteProbability)
    {
        std::vector<int> order(catalog.size());
        for (std::size_t index = 0; index < order.size(); ++index)
        {
            order[index] = static_cast<int>(index) + 1;
        }
        std::shuffle(order.begin(), order.end(), rng);

        std::uniform_real_distribution<double> unit(0.0, 1.0);
        std::vector<std::vector<int>> routes;
        const auto hasCustomer = [&](const std::vector<int> &route, int visit)
        {
            const int node = catalog[static_cast<std::size_t>(visit) - 1].nodeIndex;
            return std::any_of(route.begin(), route.end(), [&](int other)
                               { return catalog[static_cast<std::size_t>(other) - 1].nodeIndex == node; });
        };
        for (int visit : order)
        {
            if (routes.empty() || unit(rng) < newRouteProbability)
            {
                routes.push_back({visit});
                continue;
            }
            const std::size_t start = std::uniform_int_distribution<std::size_t>(0, routes.size() - 1)(rng);
            bool placed = false;
            for (std::size_t offset = 0; offset < routes.size() && !placed; ++offset)
            {
                auto &route = routes[(start + offset) % routes.size()];
                if (!hasCustomer(route, visit))
                {
                    route.push_back(visit);
                    placed = true;
                }
            }
            if (!placed)
            {
                routes.push_back({visit});
            }
        }
        return routes;
    }
}
