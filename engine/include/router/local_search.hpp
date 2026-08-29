#pragma once

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    Solution localSearch(const Instance &inst, Solution sol);
}
