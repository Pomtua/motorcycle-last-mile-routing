#pragma once

#include <cstddef>
#include <vector>

#include "router/hgs/delivery.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/search_context.hpp"

namespace router::hgs
{
    bool isCompletePermutation(const std::vector<int> &tokens, std::size_t tokenCount);

    std::vector<int> tokenSequence(const ProblemData &data, const Individual &individual);
    std::vector<Delivery> deliveriesFromTokens(const ProblemData &data, const std::vector<int> &tokens);

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
