#pragma once

#include <cstddef>
#include <vector>

#include "router/hgs/search_context.hpp"

namespace router::hgs
{
    std::vector<int> orderedCrossover(
        const std::vector<int> &first,
        const std::vector<int> &second,
        std::size_t begin,
        std::size_t end);

    std::vector<int> orderedCrossover(
        const std::vector<int> &first,
        const std::vector<int> &second,
        Rng &rng);
}
