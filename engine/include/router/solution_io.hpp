#pragma once

#include <string>

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    Solution loadSolution(const std::string &path, const Instance &inst);
}