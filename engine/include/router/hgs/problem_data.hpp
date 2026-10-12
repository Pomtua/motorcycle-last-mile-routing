#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "router/hgs/delivery.hpp"
#include "router/instance.hpp"
#include "router/split.hpp"

namespace router::hgs
{
    struct CustomerData
    {
        int node = 0;
        std::int64_t demandUnits = 0;
        double demandVolume = 0.0;
        double volumePerUnit = 0.0;
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
            const std::vector<Visit> &canonicalChunks,
            const ProblemOptions &options = {});

        std::size_t customerCount() const { return customers_.size() - 1; }
        const CustomerData &customer(int index) const { return customers_[static_cast<std::size_t>(index)]; }

        std::size_t tokenCount() const { return tokens_.size() - 1; }
        const Delivery &token(int index) const { return tokens_[static_cast<std::size_t>(index)]; }
        const std::vector<int> &tokensOf(int customer) const { return tokensOfCustomer_[static_cast<std::size_t>(customer)]; }

        double distance(int from, int to) const { return distance_[matrixIndex(from, to)]; }
        double duration(int from, int to) const { return duration_[matrixIndex(from, to)]; }
        double volumeOf(const Delivery &delivery) const
        {
            return static_cast<double>(delivery.units) * customer(delivery.customer).volumePerUnit;
        }

        const std::vector<int> &neighbours(int customer) const { return neighbours_[static_cast<std::size_t>(customer)]; }

        int fleetSize() const { return fleetSize_; }
        double weightCapacity() const { return weightCapacity_; }
        double volumeCapacity() const { return volumeCapacity_; }
        double horizon() const { return horizon_; }
        double zonePenalty() const { return zonePenalty_; }
        bool hasZones() const { return hasZones_; }

    private:
        std::size_t matrixIndex(int from, int to) const
        {
            return static_cast<std::size_t>(from) * customers_.size() + static_cast<std::size_t>(to);
        }

        void buildNeighbours(std::size_t granularity);

        std::vector<CustomerData> customers_;
        std::vector<Delivery> tokens_;
        std::vector<std::vector<int>> tokensOfCustomer_;
        std::vector<double> distance_;
        std::vector<double> duration_;
        std::vector<std::vector<int>> neighbours_;
        int fleetSize_ = 0;
        double weightCapacity_ = 0.0;
        double volumeCapacity_ = 0.0;
        double horizon_ = 0.0;
        double zonePenalty_ = 0.0;
        bool hasZones_ = false;
    };
}
