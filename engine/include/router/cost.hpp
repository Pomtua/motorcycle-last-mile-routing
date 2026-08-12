#pragma once

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    double computeCost(const Instance &inst, const Solution &solution);
}