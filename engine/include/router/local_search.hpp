#pragma once

#include <vector>

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    Solution localSearch(const Instance &inst, Solution sol);
    Solution localSearch(
        const Instance &inst,
        Solution sol,
        const std::vector<int> &zoneOf,
        double zonePenalty);
}
