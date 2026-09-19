#pragma once

#include <vector>

#include "router/instance.hpp"

namespace router
{
    std::vector<int> assignZones(const Instance &inst, int numZones);
}
