#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/crossover.hpp"
#include "router/hgs/individual.hpp"

using hgs_test::expect;
using hgs_test::expectInvalidArgument;

namespace
{
    std::vector<int> randomTour(std::mt19937 &rng, std::size_t count)
    {
        std::vector<int> tour(count);
        std::iota(tour.begin(), tour.end(), 1);
        std::shuffle(tour.begin(), tour.end(), rng);
        return tour;
    }

    std::vector<int> fillOrder(const std::vector<int> &second, const std::vector<int> &segment, std::size_t end)
    {
        std::vector<int> order;
        for (std::size_t offset = 0; offset < second.size(); ++offset)
        {
            const int visit = second[(end + offset) % second.size()];
            if (std::find(segment.begin(), segment.end(), visit) == segment.end())
            {
                order.push_back(visit);
            }
        }
        return order;
    }

    void testOrderedCrossoverDefinition()
    {
        std::mt19937 rng(600);
        bool keepsSegment = true;
        bool followsSecondParent = true;
        bool permutations = true;
        for (int trial = 0; trial < 2000; ++trial)
        {
            const std::size_t count = 1 + trial % 40;
            const auto first = randomTour(rng, count);
            const auto second = randomTour(rng, count);
            std::uniform_int_distribution<std::size_t> draw(0, count - 1);
            const std::size_t a = draw(rng);
            const std::size_t b = draw(rng);
            const std::size_t begin = std::min(a, b);
            const std::size_t end = std::max(a, b) + 1;

            const auto child = router::hgs::orderedCrossover(first, second, begin, end);
            const std::vector<int> segment(first.begin() + static_cast<std::ptrdiff_t>(begin),
                                           first.begin() + static_cast<std::ptrdiff_t>(end));
            std::vector<int> filled;
            for (std::size_t offset = 0; offset < count - segment.size(); ++offset)
            {
                filled.push_back(child[(end + offset) % count]);
            }

            permutations = permutations && router::hgs::isCompletePermutation(child, count);
            keepsSegment = keepsSegment && std::equal(segment.begin(), segment.end(),
                                                      child.begin() + static_cast<std::ptrdiff_t>(begin));
            followsSecondParent = followsSecondParent && filled == fillOrder(second, segment, end);
        }
        expect(permutations, "offspring must be complete permutations");
        expect(keepsSegment, "offspring must keep the first parent's segment in place");
        expect(followsSecondParent,
               "positions after the segment must follow the second parent's order starting after the segment");

        router::hgs::Rng firstRng(601);
        router::hgs::Rng secondRng(601);
        const auto first = randomTour(rng, 25);
        const auto second = randomTour(rng, 25);
        expect(router::hgs::orderedCrossover(first, second, firstRng) == router::hgs::orderedCrossover(first, second, secondRng),
               "random cuts must be reproducible with the same seed");
    }

    void testValidation()
    {
        const std::vector<int> parent{1, 2, 3, 4};
        expectInvalidArgument([&]() { router::hgs::orderedCrossover(parent, parent, 2, 2); }, "empty segments must be rejected");
        expectInvalidArgument([&]() { router::hgs::orderedCrossover(parent, parent, 1, 5); }, "segments past the end must be rejected");
        expectInvalidArgument([&]() { router::hgs::orderedCrossover(parent, {1, 2, 3}, 0, 1); }, "parents of different length must be rejected");
        expectInvalidArgument([&]() { router::hgs::orderedCrossover(parent, {1, 2, 2, 4}, 0, 1); }, "non-permutation parents must be rejected");
        router::hgs::Rng rng(1);
        expect(router::hgs::orderedCrossover({}, {}, rng).empty(), "empty parents must give an empty child");
        expect(router::hgs::orderedCrossover({1, 2, 3, 4, 5}, {5, 4, 3, 2, 1}, 1, 3) == std::vector<int>({4, 2, 3, 1, 5}),
               "a hand-worked example must fill from the second parent after the segment");
    }
}

int main()
{
    testOrderedCrossoverDefinition();
    testValidation();
    return hgs_test::finish("hgs crossover");
}
