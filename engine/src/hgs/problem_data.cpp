#include "router/hgs/problem_data.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace router::hgs
{
    namespace
    {
        bool isFiniteNonNegative(double value)
        {
            return std::isfinite(value) && value >= 0.0;
        }

        void validateInstance(const Instance &inst)
        {
            const std::size_t nodeCount = inst.nodes.size();
            if (inst.n < 0 || nodeCount != static_cast<std::size_t>(inst.n) + 1)
            {
                throw std::invalid_argument("instance must have n customers plus one depot node");
            }
            if (inst.fleet.size < 0)
            {
                throw std::invalid_argument("fleet size must be non-negative");
            }
            if (!std::isfinite(inst.fleet.weightCapacity) || inst.fleet.weightCapacity <= 0.0 ||
                !std::isfinite(inst.fleet.volumeCapacity) || inst.fleet.volumeCapacity <= 0.0)
            {
                throw std::invalid_argument("fleet capacities must be finite and positive");
            }
            if (!isFiniteNonNegative(inst.horizon))
            {
                throw std::invalid_argument("horizon must be finite and non-negative");
            }
            for (const auto *matrix : {&inst.distanceMatrix, &inst.durationMatrix})
            {
                if (matrix->size() != nodeCount)
                {
                    throw std::invalid_argument("matrices must be square over all nodes");
                }
                for (const auto &row : *matrix)
                {
                    if (row.size() != nodeCount ||
                        !std::all_of(row.begin(), row.end(), isFiniteNonNegative))
                    {
                        throw std::invalid_argument("matrix entries must be finite and non-negative");
                    }
                }
            }
            for (const Node &node : inst.nodes)
            {
                if (node.serviceTime < 0 || node.twStart > node.twEnd)
                {
                    throw std::invalid_argument("nodes need non-negative service and ordered time windows");
                }
            }
        }

        std::vector<double> flatten(const std::vector<std::vector<double>> &matrix)
        {
            std::vector<double> flat;
            flat.reserve(matrix.size() * matrix.size());
            for (const auto &row : matrix)
            {
                flat.insert(flat.end(), row.begin(), row.end());
            }
            return flat;
        }
    }

    ProblemData::ProblemData(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const ProblemOptions &options)
    {
        validateInstance(inst);
        if (options.granularity == 0)
        {
            throw std::invalid_argument("granularity must be positive");
        }
        if (!isFiniteNonNegative(options.zonePenalty))
        {
            throw std::invalid_argument("zone penalty must be finite and non-negative");
        }
        hasZones_ = !options.zoneOf.empty();
        if (hasZones_ && options.zoneOf.size() != inst.nodes.size())
        {
            throw std::invalid_argument("zone assignment size must match node count");
        }
        if (!hasZones_ && options.zonePenalty > 0.0)
        {
            throw std::invalid_argument("zone penalty requires a zone assignment");
        }

        nodeCount_ = inst.nodes.size();
        distance_ = flatten(inst.distanceMatrix);
        duration_ = flatten(inst.durationMatrix);
        fleetSize_ = inst.fleet.size;
        weightCapacity_ = inst.fleet.weightCapacity;
        volumeCapacity_ = inst.fleet.volumeCapacity;
        horizon_ = inst.horizon;
        zonePenalty_ = options.zonePenalty;
        catalog_ = visitCatalog;

        visits_.reserve(visitCatalog.size() + 1);
        visits_.push_back({0, 0.0, 0.0, 0.0, 0.0, inst.horizon, 0});
        for (const Visit &visit : visitCatalog)
        {
            if (visit.nodeIndex <= 0 || visit.nodeIndex > inst.n)
            {
                throw std::invalid_argument("visit node index must refer to a customer");
            }
            if (!isFiniteNonNegative(visit.weight) || !isFiniteNonNegative(visit.volume))
            {
                throw std::invalid_argument("visit load must be finite and non-negative");
            }
            const Node &node = inst.nodes[static_cast<std::size_t>(visit.nodeIndex)];
            std::uint64_t zoneMask = 0;
            if (hasZones_)
            {
                const int zone = options.zoneOf[static_cast<std::size_t>(visit.nodeIndex)];
                if (zone < 0 || zone >= kMaxZones)
                {
                    throw std::invalid_argument(
                        "customer zone must be in [0, " + std::to_string(kMaxZones) + ")");
                }
                zoneMask = std::uint64_t{1} << zone;
            }
            visits_.push_back({
                visit.nodeIndex,
                visit.weight,
                visit.volume,
                static_cast<double>(node.serviceTime),
                static_cast<double>(node.twStart),
                static_cast<double>(node.twEnd),
                zoneMask});
        }

        siblings_.assign(visits_.size(), {});
        std::vector<std::vector<int>> visitsByNode(nodeCount_);
        for (int index = 1; index < static_cast<int>(visits_.size()); ++index)
        {
            visitsByNode[static_cast<std::size_t>(visits_[static_cast<std::size_t>(index)].node)].push_back(index);
        }
        for (const auto &group : visitsByNode)
        {
            for (int member : group)
            {
                for (int other : group)
                {
                    if (other != member)
                    {
                        siblings_[static_cast<std::size_t>(member)].push_back(other);
                    }
                }
            }
        }

        buildNeighbours(options.granularity);
    }

    void ProblemData::buildNeighbours(std::size_t granularity)
    {
        neighbours_.assign(visits_.size(), {});
        std::vector<std::pair<double, int>> candidates;
        for (int index = 1; index < static_cast<int>(visits_.size()); ++index)
        {
            candidates.clear();
            const int node = visits_[static_cast<std::size_t>(index)].node;
            for (int other = 1; other < static_cast<int>(visits_.size()); ++other)
            {
                if (visits_[static_cast<std::size_t>(other)].node == node)
                {
                    continue;
                }
                const double proximity = 0.5 * (distance(index, other) + distance(other, index));
                candidates.emplace_back(proximity, other);
            }
            const std::size_t keep = std::min(granularity, candidates.size());
            std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(keep), candidates.end());
            auto &list = neighbours_[static_cast<std::size_t>(index)];
            list.reserve(keep);
            for (std::size_t rank = 0; rank < keep; ++rank)
            {
                list.push_back(candidates[rank].second);
            }
        }
    }
}
