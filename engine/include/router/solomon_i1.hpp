#pragma once

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    Solution solomonI1(const Instance &inst, bool enforceFleet = true);
}