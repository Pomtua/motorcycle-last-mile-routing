#pragma once

#include <string>
#include <vector>

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{

    struct ValidationReport
    {
        bool feasible = true;
        std::vector<std::string> violations;

        void addViolation(std::string msg)
        {
            feasible = false;
            violations.push_back(std::move(msg));
        }
    };

    ValidationReport validate(const Instance &inst, const Solution &solution);

}