#include "router/zones.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>

namespace router
{
    namespace
    {
        struct Point
        {
            double x = 0.0;
            double y = 0.0;
        };

        double distanceSquared(const Point &a, const Point &b)
        {
            const double dx = a.x - b.x;
            const double dy = a.y - b.y;
            return dx * dx + dy * dy;
        }
    }

    std::vector<int> assignZones(const Instance &inst, int numZones)
    {
        if (inst.n <= 0 ||
            inst.nodes.size() != static_cast<std::size_t>(inst.n) + 1 ||
            numZones < 1 || numZones > inst.n)
        {
            throw std::invalid_argument("assignZones: invalid instance size or zone count");
        }

        double meanLatitude = 0.0;
        for (int i = 1; i <= inst.n; ++i)
        {
            const Node &node = inst.nodes[static_cast<std::size_t>(i)];
            if (!std::isfinite(node.lat) || !std::isfinite(node.lng))
            {
                throw std::invalid_argument("assignZones: non-finite customer coordinate");
            }
            meanLatitude += node.lat;
        }
        meanLatitude /= inst.n;

        const double longitudeScale =
            std::cos(meanLatitude * std::numbers::pi_v<double> / 180.0);
        std::vector<Point> points(static_cast<std::size_t>(inst.n) + 1);
        for (int i = 1; i <= inst.n; ++i)
        {
            const Node &node = inst.nodes[static_cast<std::size_t>(i)];
            points[static_cast<std::size_t>(i)] =
                {node.lng * longitudeScale, node.lat};
        }

        std::mt19937 rng(static_cast<unsigned>(inst.seed));
        std::uniform_int_distribution<int> firstCustomer(1, inst.n);
        std::vector<Point> centers;
        centers.reserve(static_cast<std::size_t>(numZones));
        centers.push_back(points[static_cast<std::size_t>(firstCustomer(rng))]);

        for (int c = 1; c < numZones; ++c)
        {
            std::vector<double> weights(static_cast<std::size_t>(inst.n));
            double totalWeight = 0.0;
            for (int i = 1; i <= inst.n; ++i)
            {
                double nearest = std::numeric_limits<double>::infinity();
                for (const Point &center : centers)
                {
                    nearest = std::min(
                        nearest,
                        distanceSquared(points[static_cast<std::size_t>(i)], center));
                }
                weights[static_cast<std::size_t>(i - 1)] = nearest;
                totalWeight += nearest;
            }
            if (!std::isfinite(totalWeight) || totalWeight <= 0.0)
            {
                throw std::invalid_argument(
                    "assignZones: zone count exceeds distinct customer locations");
            }
            std::discrete_distribution<int> nextCustomer(
                weights.begin(), weights.end());
            centers.push_back(
                points[static_cast<std::size_t>(nextCustomer(rng) + 1)]);
        }

        std::vector<int> zones(static_cast<std::size_t>(inst.n) + 1, -1);
        for (int iteration = 0; iteration < 300; ++iteration)
        {
            bool changed = false;
            std::vector<Point> sums(static_cast<std::size_t>(numZones));
            std::vector<int> counts(static_cast<std::size_t>(numZones), 0);

            for (int i = 1; i <= inst.n; ++i)
            {
                const Point &point = points[static_cast<std::size_t>(i)];
                int bestZone = 0;
                double bestDistance = distanceSquared(point, centers[0]);
                for (int c = 1; c < numZones; ++c)
                {
                    const double candidate =
                        distanceSquared(point, centers[static_cast<std::size_t>(c)]);
                    if (candidate < bestDistance)
                    {
                        bestDistance = candidate;
                        bestZone = c;
                    }
                }

                if (zones[static_cast<std::size_t>(i)] != bestZone)
                {
                    changed = true;
                }
                zones[static_cast<std::size_t>(i)] = bestZone;
                ++counts[static_cast<std::size_t>(bestZone)];
                sums[static_cast<std::size_t>(bestZone)].x += point.x;
                sums[static_cast<std::size_t>(bestZone)].y += point.y;
            }

            for (int c = 0; c < numZones; ++c)
            {
                if (counts[static_cast<std::size_t>(c)] != 0)
                {
                    continue;
                }

                int donor = -1;
                double farthest = -1.0;
                for (int i = 1; i <= inst.n; ++i)
                {
                    const int oldZone = zones[static_cast<std::size_t>(i)];
                    if (counts[static_cast<std::size_t>(oldZone)] <= 1)
                    {
                        continue;
                    }
                    const double distance = distanceSquared(
                        points[static_cast<std::size_t>(i)],
                        centers[static_cast<std::size_t>(oldZone)]);
                    if (distance > farthest)
                    {
                        farthest = distance;
                        donor = i;
                    }
                }
                if (donor < 0)
                {
                    throw std::runtime_error("assignZones: cannot repair empty zone");
                }

                const int oldZone = zones[static_cast<std::size_t>(donor)];
                const Point &point = points[static_cast<std::size_t>(donor)];
                zones[static_cast<std::size_t>(donor)] = c;
                --counts[static_cast<std::size_t>(oldZone)];
                ++counts[static_cast<std::size_t>(c)];
                sums[static_cast<std::size_t>(oldZone)].x -= point.x;
                sums[static_cast<std::size_t>(oldZone)].y -= point.y;
                sums[static_cast<std::size_t>(c)].x += point.x;
                sums[static_cast<std::size_t>(c)].y += point.y;
                changed = true;
            }

            for (int c = 0; c < numZones; ++c)
            {
                centers[static_cast<std::size_t>(c)] = {
                    sums[static_cast<std::size_t>(c)].x /
                        counts[static_cast<std::size_t>(c)],
                    sums[static_cast<std::size_t>(c)].y /
                        counts[static_cast<std::size_t>(c)]};
            }
            if (!changed)
            {
                return zones;
            }
        }

        return zones;
    }
}
