#include "router/hgs/crossover.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>

#include "router/hgs/individual.hpp"

namespace router::hgs
{
    namespace
    {
        void validateParents(const std::vector<int> &first, const std::vector<int> &second)
        {
            if (!isCompletePermutation(first, first.size()) || !isCompletePermutation(second, first.size()))
            {
                throw std::invalid_argument("crossover parents must be permutations of the same visits");
            }
        }
    }

    std::vector<int> orderedCrossover(
        const std::vector<int> &first,
        const std::vector<int> &second,
        std::size_t begin,
        std::size_t end)
    {
        validateParents(first, second);
        const std::size_t count = first.size();
        if (count == 0)
        {
            return {};
        }
        if (begin >= end || end > count)
        {
            throw std::invalid_argument("crossover segment must be a non-empty half-open range");
        }

        std::vector<int> child(count, 0);
        std::vector<bool> used(count + 1, false);
        for (std::size_t index = begin; index < end; ++index)
        {
            child[index] = first[index];
            used[static_cast<std::size_t>(first[index])] = true;
        }

        std::size_t write = end % count;
        for (std::size_t scanned = 0, read = end % count; scanned < count; ++scanned, read = (read + 1) % count)
        {
            const int visit = second[read];
            if (used[static_cast<std::size_t>(visit)])
            {
                continue;
            }
            child[write] = visit;
            used[static_cast<std::size_t>(visit)] = true;
            write = (write + 1) % count;
        }
        return child;
    }

    std::vector<int> orderedCrossover(
        const std::vector<int> &first,
        const std::vector<int> &second,
        Rng &rng)
    {
        validateParents(first, second);
        if (first.empty())
        {
            return {};
        }
        std::uniform_int_distribution<std::size_t> draw(0, first.size() - 1);
        const std::size_t a = draw(rng);
        const std::size_t b = draw(rng);
        return orderedCrossover(first, second, std::min(a, b), std::max(a, b) + 1);
    }
}
