#pragma once

#include <vector>

#include "router/hgs/search_context.hpp"

namespace router::hgs
{
    std::vector<int> randomTour(std::vector<int> tour, Rng &rng);
    std::vector<int> perturbedTour(std::vector<int> tour, double strength, Rng &rng);
}
