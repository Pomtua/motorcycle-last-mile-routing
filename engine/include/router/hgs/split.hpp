#pragma once

#include <optional>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/problem_data.hpp"

namespace router::hgs
{
    inline constexpr double kSplitLoadLimitFactor = 2.0;

    std::optional<Individual> split(
        const ProblemData &data,
        const std::vector<int> &giantTour,
        const Penalties &penalties);
}
