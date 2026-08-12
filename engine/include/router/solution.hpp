#pragma once

#include <vector>

#include "router/split.hpp"

namespace router
{
    struct Route
    {
        std::vector<Visit> stops;
    };

    struct Solution
    {
        std::vector<Route> routes;
    };

}