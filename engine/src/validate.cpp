#include "router/validate.hpp"

#include <map>
#include <set>

#include "router/split.hpp"

namespace router
{
    namespace
    {
        void checkCoverageAndDuplicates(const Instance &inst, const Solution &solution,
                                        ValidationReport &report)
        {
            std::set<std::pair<int, int>> required;
            for (const Visit &v : splitCustomers(inst))
            {
                required.insert({v.nodeIndex, v.chunkIdx});
            }

            std::set<std::pair<int, int>> servedOverall;

            for (std::size_t r = 0; r < solution.routes.size(); ++r)
            {
                std::set<int> customersInThisRoute;

                for (const Visit &stop : solution.routes[r].stops)
                {
                    const auto key = std::make_pair(stop.nodeIndex, stop.chunkIdx);

                    if (!servedOverall.insert(key).second)
                    {
                        report.addViolation(
                            "chunk (nodeIndex=" + std::to_string(stop.nodeIndex) +
                            ", chunkIdx=" + std::to_string(stop.chunkIdx) +
                            ") is served more than once across the whole solution");
                    }

                    if (!customersInThisRoute.insert(stop.nodeIndex).second)
                    {
                        report.addViolation(
                            "route " + std::to_string(r) + " serves two chunks of the same customer " +
                            "(nodeIndex=" + std::to_string(stop.nodeIndex) + ") — not allowed");
                    }
                }
            }

            for (const auto &key : required)
            {
                if (servedOverall.find(key) == servedOverall.end())
                {
                    report.addViolation(
                        "chunk (nodeIndex=" + std::to_string(key.first) +
                        ", chunkIdx=" + std::to_string(key.second) + ") was never served");
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
        }

    }

    ValidationReport validate(const Instance &inst, const Solution &solution)
    {
        ValidationReport report;
        checkCoverageAndDuplicates(inst, solution, report);
        checkCapacity(inst, solution, report);
        checkTimeWindows(inst, solution, report);
        checkFleetSize(inst, solution, report);
        return report;
    }

}