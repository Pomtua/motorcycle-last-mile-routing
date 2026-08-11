#pragma once

#include <vector>

#include "router/instance.hpp"

namespace router
{
    struct Visit
    {
        int nodeIndex = 0;
        int chunkIdx = 0;
        int totalChunks = 1;

        double weight = 0.0;
        double volume = 0.0;
    };

    std::vector<Visit> splitCustomers(const Instance &inst);
}