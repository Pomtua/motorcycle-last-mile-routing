#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs.hpp"
#include "router/hgs/crossover.hpp"
#include "router/hgs/individual.hpp"

using hgs_test::expect;
using hgs_test::expectInvalidArgument;

namespace
{
    std::vector<router::HgsGene> toGenes(const std::vector<int> &tour)
    {
        std::vector<router::HgsGene> genes;
        for (int visit : tour)
        {
            genes.push_back({static_cast<std::size_t>(visit) - 1});
        }
        return genes;
    }

    std::vector<int> fromGenes(const std::vector<router::HgsGene> &genes)
    {
        std::vector<int> tour;
        for (const auto &gene : genes)
        {
            tour.push_back(static_cast<int>(gene.visitIndex) + 1);
        }
        return tour;
    }

    std::vector<int> randomTour(std::mt19937 &rng, std::size_t count)
    {
        std::vector<int> tour(count);
        std::iota(tour.begin(), tour.end(), 1);
        std::shuffle(tour.begin(), tour.end(), rng);
        return tour;
    }

    void testAgainstV1()
    {
        std::mt19937 rng(600);
        bool matches = true;
        bool keepsSegment = true;
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
            matches = matches && child == fromGenes(router::orderedCrossover(toGenes(first), toGenes(second), begin, end));
            permutations = permutations && router::hgs::isCompletePermutation(child, count);
            keepsSegment = keepsSegment && std::equal(child.begin() + static_cast<std::ptrdiff_t>(begin),
                                                      child.begin() + static_cast<std::ptrdiff_t>(end),
                                                      first.begin() + static_cast<std::ptrdiff_t>(begin));
        }
        expect(matches, "ordered crossover must match v1 for every cut");
        expect(permutations, "offspring must be complete permutations");
        expect(keepsSegment, "offspring must keep the first parent's segment in place");

        router::hgs::Rng v2Rng(601);
        std::mt19937 v1Rng(601);
        bool randomCutsMatch = true;
        for (int trial = 0; trial < 200; ++trial)
        {
            const auto first = randomTour(rng, 25);
            const auto second = randomTour(rng, 25);
            randomCutsMatch = randomCutsMatch &&
                              router::hgs::orderedCrossover(first, second, v2Rng) ==
                                  fromGenes(router::orderedCrossover(toGenes(first), toGenes(second), v1Rng));
        }
        expect(randomCutsMatch, "random cuts must be drawn exactly like v1");
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
    }
}

int main()
{
    testAgainstV1();
    testValidation();
    return hgs_test::finish("hgs crossover");
}
