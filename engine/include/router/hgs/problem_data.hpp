#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "router/instance.hpp"
#include "router/split.hpp"

namespace router::hgs
{
    struct VisitData
    {
        int node = 0;
        double weight = 0.0;
        double volume = 0.0;
        double serviceTime = 0.0;
        double twStart = 0.0;
        double twEnd = 0.0;
        std::uint64_t zoneMask = 0;
    };

    struct ProblemOptions
    {
        std::vector<int> zoneOf;
        double zonePenalty = 0.0;
        std::size_t granularity = 20;
    };

    class ProblemData
    {
    public:
        static constexpr int kDepot = 0;
        static constexpr int kMaxZones = 64;

        ProblemData(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const ProblemOptions &options = {});

        std::size_t visitCount() const { return visits_.size() - 1; }
        const VisitData &visit(int index) const { return visits_[static_cast<std::size_t>(index)]; }
        const Visit &catalogVisit(int index) const { return catalog_[static_cast<std::size_t>(index) - 1]; }

        double distance(int from, int to) const
        {
            return distance_[matrixIndex(from, to)];
        }

        double duration(int from, int to) const
        {
            return duration_[matrixIndex(from, to)];
        }

        const std::vector<int> &neighbours(int index) const { return neighbours_[static_cast<std::size_t>(index)]; }
        const std::vector<int> &siblings(int index) const { return siblings_[static_cast<std::size_t>(index)]; }

        int fleetSize() const { return fleetSize_; }
        double weightCapacity() const { return weightCapacity_; }
        double volumeCapacity() const { return volumeCapacity_; }
        double horizon() const { return horizon_; }
        double zonePenalty() const { return zonePenalty_; }
        bool hasZones() const { return hasZones_; }

    private:
        std::size_t matrixIndex(int from, int to) const
        {
            return static_cast<std::size_t>(visits_[static_cast<std::size_t>(from)].node) * nodeCount_ +
                   static_cast<std::size_t>(visits_[static_cast<std::size_t>(to)].node);
        }

        void buildNeighbours(std::size_t granularity);

        std::size_t nodeCount_ = 0;
        std::vector<VisitData> visits_;
        std::vector<Visit> catalog_;
        std::vector<double> distance_;
        std::vector<double> duration_;
        std::vector<std::vector<int>> neighbours_;
        std::vector<std::vector<int>> siblings_;
        int fleetSize_ = 0;
        double weightCapacity_ = 0.0;
        double volumeCapacity_ = 0.0;
        double horizon_ = 0.0;
        double zonePenalty_ = 0.0;
        bool hasZones_ = false;
    };
}
