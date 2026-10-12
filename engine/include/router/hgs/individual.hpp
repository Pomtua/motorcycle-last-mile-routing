#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/delivery.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/solution.hpp"

namespace router::hgs
{
    using Arc = std::pair<int, int>;

    struct Individual
    {
        std::vector<Delivery> giantTour;
        std::vector<std::vector<Delivery>> routes;
        std::vector<Arc> arcs;
        CostBreakdown cost;
    };

    struct SplitSummary
    {
        std::size_t customers = 0;
        std::size_t deliveries = 0;
        std::size_t canonicalChunks = 0;
        std::size_t splitCustomers = 0;
        std::size_t maxDeliveriesPerCustomer = 0;
        std::size_t deliveriesMatchingCanonical = 0;
        double smallestPieceFraction = 1.0;
        std::vector<std::size_t> pieceFractionHistogram;
    };

    inline constexpr double kSplitHistogramEdges[] = {0.1, 0.25, 0.5, 1.0};

    bool isCompleteDeliverySet(const ProblemData &data, const std::vector<Delivery> &deliveries);
    SplitSummary summarizeSplits(const ProblemData &data, const Individual &individual);

    Individual makeIndividual(const ProblemData &data, std::vector<std::vector<Delivery>> routes);
    Individual individualFromSolution(const ProblemData &data, const Solution &solution);
    Solution toSolution(const ProblemData &data, const Individual &individual);
}
