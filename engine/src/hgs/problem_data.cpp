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
                if (!isFiniteNonNegative(node.demandWeight) || !isFiniteNonNegative(node.demandVolume))
                {
                    throw std::invalid_argument("node demand must be finite and non-negative");
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

        std::uint64_t zoneMaskOf(const ProblemOptions &options, int node)
        {
            if (options.zoneOf.empty())
            {
                return 0;
            }
            const int zone = options.zoneOf[static_cast<std::size_t>(node)];
            if (zone < 0 || zone >= ProblemData::kMaxZones)
            {
                throw std::invalid_argument(
                    "customer zone must be in [0, " + std::to_string(ProblemData::kMaxZones) + ")");
            }
            return std::uint64_t{1} << zone;
        }
    }

    ProblemData::ProblemData(
        const Instance &inst,
        const std::vector<Visit> &canonicalChunks,
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
        if (!std::isfinite(options.minDeliveryFraction) || options.minDeliveryFraction < 0.0 ||
            options.minDeliveryFraction >= 1.0)
        {
            throw std::invalid_argument("minimum delivery fraction must be in [0, 1)");
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

        distance_ = flatten(inst.distanceMatrix);
        duration_ = flatten(inst.durationMatrix);
        fleetSize_ = inst.fleet.size;
        weightCapacity_ = inst.fleet.weightCapacity;
        volumeCapacity_ = inst.fleet.volumeCapacity;
        horizon_ = inst.horizon;
        zonePenalty_ = options.zonePenalty;
        minDeliveryFraction_ = options.minDeliveryFraction;

        customers_.reserve(inst.nodes.size());
        customers_.push_back({0, 0, 0.0, 0.0, 0.0, 0.0, inst.horizon, 0});
        for (int node = 1; node <= inst.n; ++node)
        {
            const Node &source = inst.nodes[static_cast<std::size_t>(node)];
            const std::int64_t units = toUnits(source.demandWeight);
            if (units == 0 && source.demandVolume > 0.0)
            {
                throw std::invalid_argument("a customer with volume demand must also have weight demand");
            }
            customers_.push_back({
                node,
                units,
                source.demandVolume,
                units > 0 ? source.demandVolume / static_cast<double>(units) : 0.0,
                static_cast<double>(source.serviceTime),
                static_cast<double>(source.twStart),
                static_cast<double>(source.twEnd),
                zoneMaskOf(options, node)});
        }

        tokens_.reserve(canonicalChunks.size() + 1);
        tokens_.push_back({});
        tokensOfCustomer_.assign(customers_.size(), {});
        std::vector<std::int64_t> coveredUnits(customers_.size(), 0);
        for (const Visit &chunk : canonicalChunks)
        {
            if (chunk.nodeIndex <= 0 || chunk.nodeIndex > inst.n)
            {
                throw std::invalid_argument("canonical chunk must refer to a customer");
            }
            if (!isFiniteNonNegative(chunk.weight) || toUnits(chunk.weight) <= 0)
            {
                throw std::invalid_argument("canonical chunk weight must be positive");
            }
            const Delivery delivery{chunk.nodeIndex, toUnits(chunk.weight)};
            tokensOfCustomer_[static_cast<std::size_t>(delivery.customer)].push_back(static_cast<int>(tokens_.size()));
            tokens_.push_back(delivery);
            coveredUnits[static_cast<std::size_t>(delivery.customer)] += delivery.units;
        }
        for (int index = 1; index < static_cast<int>(customers_.size()); ++index)
        {
            if (coveredUnits[static_cast<std::size_t>(index)] != customers_[static_cast<std::size_t>(index)].demandUnits)
            {
                throw std::invalid_argument(
                    "canonical chunks must add up to the demand of customer " + std::to_string(index));
            }
        }

        minDeliveryUnits_.assign(customers_.size(), 1);
        for (std::size_t index = 1; index < customers_.size(); ++index)
        {
            const auto required = static_cast<std::int64_t>(
                std::ceil(options.minDeliveryFraction * static_cast<double>(customers_[index].demandUnits) - 1e-9));
            minDeliveryUnits_[index] = std::max<std::int64_t>(1, required);
        }

        buildNeighbours(options.granularity);
    }

    void ProblemData::buildNeighbours(std::size_t granularity)
    {
        neighbours_.assign(customers_.size(), {});
        std::vector<std::pair<double, int>> candidates;
        for (int index = 1; index < static_cast<int>(customers_.size()); ++index)
        {
            if (customer(index).demandUnits == 0)
            {
                continue;
            }
            candidates.clear();
            for (int other = 1; other < static_cast<int>(customers_.size()); ++other)
            {
                if (other == index || customer(other).demandUnits == 0)
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
