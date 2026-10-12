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

    bool isCompleteDeliverySet(const ProblemData &data, const std::vector<Delivery> &deliveries);

    Individual makeIndividual(const ProblemData &data, std::vector<std::vector<Delivery>> routes);
    Individual individualFromSolution(const ProblemData &data, const Solution &solution);
    Solution toSolution(const ProblemData &data, const Individual &individual);
}
