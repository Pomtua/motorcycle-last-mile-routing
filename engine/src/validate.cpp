#include "router/validate.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace router
{
    namespace
    {
        constexpr double kWeightTolerance = 1e-6;
        constexpr double kVolumeTolerance = 1e-6;
        constexpr double kWeightResolution = 0.01;
        constexpr double kVolumeResolution = 1e-4;

        void checkDeliveries(const Instance &inst, const Solution &solution, ValidationReport &report)
        {
            const std::size_t nodeCount = inst.nodes.size();
            std::vector<double> deliveredWeight(nodeCount, 0.0);
            std::vector<double> deliveredVolume(nodeCount, 0.0);

            for (std::size_t r = 0; r < solution.routes.size(); ++r)
            {
                std::set<int> customersInThisRoute;
                for (const Visit &stop : solution.routes[r].stops)
                {
                    if (stop.nodeIndex <= 0 || static_cast<std::size_t>(stop.nodeIndex) >= nodeCount)
                    {
                        report.addViolation(
                            "route " + std::to_string(r) + " visits nodeIndex=" +
                            std::to_string(stop.nodeIndex) + ", which is not a customer");
                        continue;
                    }
                    if (!customersInThisRoute.insert(stop.nodeIndex).second)
                    {
                        report.addViolation(
                            "route " + std::to_string(r) + " serves the same customer twice " +
                            "(nodeIndex=" + std::to_string(stop.nodeIndex) + ") — not allowed");
                    }
                    if (!(stop.weight > kWeightTolerance))
                    {
                        report.addViolation(
                            "route " + std::to_string(r) + " delivers a non-positive weight to nodeIndex=" +
                            std::to_string(stop.nodeIndex));
                    }

                    const Node &node = inst.nodes[static_cast<std::size_t>(stop.nodeIndex)];
                    if (node.demandWeight > 0.0)
                    {
                        const double volumePerWeight = node.demandVolume / node.demandWeight;
                        const double expectedVolume = stop.weight * volumePerWeight;
                        const double chunkRounding = kWeightResolution * volumePerWeight + kVolumeResolution;
                        const double rounding = chunkRounding * std::max(1, stop.totalChunks - 1) + 1e-9;
                        if (std::abs(stop.volume - expectedVolume) > rounding)
                        {
                            report.addViolation(
                                "route " + std::to_string(r) + " delivers volume " + std::to_string(stop.volume) +
                                " to nodeIndex=" + std::to_string(stop.nodeIndex) +
                                ", not proportional to its weight (expected " + std::to_string(expectedVolume) + ")");
                        }
                    }
                    deliveredWeight[static_cast<std::size_t>(stop.nodeIndex)] += stop.weight;
                    deliveredVolume[static_cast<std::size_t>(stop.nodeIndex)] += stop.volume;
                }
            }

            for (std::size_t node = 1; node < nodeCount; ++node)
            {
                if (std::abs(deliveredWeight[node] - inst.nodes[node].demandWeight) > kWeightTolerance)
                {
                    report.addViolation(
                        "customer nodeIndex=" + std::to_string(node) + " receives weight " +
                        std::to_string(deliveredWeight[node]) + " but demands " +
                        std::to_string(inst.nodes[node].demandWeight));
                }
                if (std::abs(deliveredVolume[node] - inst.nodes[node].demandVolume) > kVolumeTolerance)
                {
                    report.addViolation(
                        "customer nodeIndex=" + std::to_string(node) + " receives volume " +
                        std::to_string(deliveredVolume[node]) + " but demands " +
                        std::to_string(inst.nodes[node].demandVolume));
                }
            }
        }

        void checkCapacity(const Instance &inst, const Solution &solution, ValidationReport &report)
        {
            for (std::size_t r = 0; r < solution.routes.size(); ++r)
            {
                double totalWeight = 0.0, totalVolume = 0.0;
                for (const Visit &stop : solution.routes[r].stops)
                {
                    totalWeight += stop.weight;
                    totalVolume += stop.volume;
                }
                if (totalWeight > inst.fleet.weightCapacity + 1e-6)
                {
                    report.addViolation(
                        "route " + std::to_string(r) + " weight " + std::to_string(totalWeight) +
                        " exceeds capacity " + std::to_string(inst.fleet.weightCapacity));
                }
                if (totalVolume > inst.fleet.volumeCapacity + 1e-9)
                {
                    report.addViolation(
                        "route " + std::to_string(r) + " volume " + std::to_string(totalVolume) +
                        " exceeds capacity " + std::to_string(inst.fleet.volumeCapacity));
                }
            }
        }

        void checkTimeWindows(const Instance &inst, const Solution &solution, ValidationReport &report)
        {
            constexpr double kTimeTolerance = 1e-6;

            for (std::size_t r = 0; r < solution.routes.size(); ++r)
            {
                const Route &route = solution.routes[r];

                double clock = 0.0;
                int currentNode = 0;

                for (const Visit &stop : route.stops)
                {
                    const double travel = inst.durationMatrix[static_cast<std::size_t>(currentNode)]
                                                             [static_cast<std::size_t>(stop.nodeIndex)];
                    double arrival = clock + travel;

                    const Node &node = inst.nodes[static_cast<std::size_t>(stop.nodeIndex)];

                    if (arrival < node.twStart)
                    {
                        arrival = node.twStart;
                    }
                    if (arrival > node.twEnd + kTimeTolerance)
                    {
                        report.addViolation(
                            "route " + std::to_string(r) + " arrives at nodeIndex=" +
                            std::to_string(stop.nodeIndex) + " (chunkIdx=" + std::to_string(stop.chunkIdx) +
                            ") at t=" + std::to_string(arrival) + ", after its window end " +
                            std::to_string(node.twEnd));
                    }

                    clock = arrival + node.serviceTime;
                    currentNode = stop.nodeIndex;
                }

                const double returnTravel =
                    inst.durationMatrix[static_cast<std::size_t>(currentNode)][0];
                const double finishTime = clock + returnTravel;

                if (finishTime > inst.horizon + kTimeTolerance)
                {
                    report.addViolation(
                        "route " + std::to_string(r) + " finishes at t=" + std::to_string(finishTime) +
                        ", after horizon " + std::to_string(inst.horizon));
                }
            }
        }

        void checkFleetSize(const Instance &inst, const Solution &solution, ValidationReport &report)
        {
            if (solution.routes.size() > static_cast<std::size_t>(inst.fleet.size))
            {
                report.addViolation(
                    "solution uses " + std::to_string(solution.routes.size()) +
                    " routes, exceeding fleet.size=" + std::to_string(inst.fleet.size));
            }

            for (std::size_t r = 0; r < solution.routes.size(); ++r)
            {
                if (solution.routes[r].stops.empty())
                {
                    report.addViolation(
                        "route " + std::to_string(r) +
                        " is empty — unused vehicles must be "
                        "omitted from the solution, not included as empty routes");
                }
            }
        }
    }
    ValidationReport validate(const Instance &inst, const Solution &solution)
    {
        ValidationReport report;
        checkDeliveries(inst, solution, report);
        checkCapacity(inst, solution, report);
        checkTimeWindows(inst, solution, report);
        checkFleetSize(inst, solution, report);
        return report;
    }

}