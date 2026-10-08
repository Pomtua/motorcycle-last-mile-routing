#pragma once

#include <cstddef>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/solution.hpp"

namespace router::hgs
{
    struct Individual
    {
        std::vector<int> giantTour;
        std::vector<std::vector<int>> routes;
        std::vector<int> successor;
        std::vector<int> predecessor;
        CostBreakdown cost;
    };

    bool isCompletePermutation(const std::vector<int> &giantTour, std::size_t visitCount);

    Individual makeIndividual(const ProblemData &data, std::vector<std::vector<int>> routes);
    Individual individualFromSolution(const ProblemData &data, const Solution &solution);
    Solution toSolution(const ProblemData &data, const Individual &individual);
}
