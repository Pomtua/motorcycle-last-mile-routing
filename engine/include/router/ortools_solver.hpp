#pragma once

#include <cstdint>
#include <vector>

#include "router/instance.hpp"
#include "router/solution.hpp"
#include "router/split.hpp"

namespace router
{
    struct OrToolsResult
    {
        bool solved = false;
        int status = 0;
        double solveMs = 0.0;
        Solution solution;
    };

    OrToolsResult solveWithOrTools(
        const Instance &inst,
        const std::vector<Visit> &chunks,
        double timeLimitSeconds,
        const std::vector<int> &zoneOf = {},
        std::int64_t zonePenalty = 0);
}
