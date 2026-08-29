#pragma once

#include <cstddef>

#include "router/instance.hpp"
#include "router/solution.hpp"

namespace router
{
    struct InsertionCandidate
    {
        std::size_t position = 0;
        double cost = 0.0;
        bool feasible = false;
    };

    InsertionCandidate bestInsertion(const Instance &inst, const Route &route,
                                     const Visit &visit);

    double removalGain(const Instance &inst, const Route &route, std::size_t position);
}
