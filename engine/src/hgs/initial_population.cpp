#include "router/hgs/initial_population.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <utility>

namespace router::hgs
{
    std::vector<int> randomTour(std::vector<int> tour, Rng &rng)
    {
        std::shuffle(tour.begin(), tour.end(), rng);
        return tour;
    }

    std::vector<int> perturbedTour(std::vector<int> tour, double strength, Rng &rng)
    {
        if (!std::isfinite(strength) || strength < 0.0 || strength > 1.0)
        {
            throw std::invalid_argument("perturbation strength must be in [0, 1]");
        }
        if (tour.size() < 2)
        {
            return tour;
        }
        const std::size_t maxMoves = std::max<std::size_t>(
            1, static_cast<std::size_t>(strength * static_cast<double>(tour.size())));
        std::uniform_int_distribution<std::size_t> drawMoves(1, maxMoves);
        std::uniform_int_distribution<std::size_t> drawIndex(0, tour.size() - 1);
        std::bernoulli_distribution drawSwap(0.5);
        const std::size_t moves = drawMoves(rng);
        for (std::size_t move = 0; move < moves; ++move)
        {
            const std::size_t from = drawIndex(rng);
            const std::size_t to = drawIndex(rng);
            if (drawSwap(rng))
            {
                std::swap(tour[from], tour[to]);
                continue;
            }
            const int visit = tour[from];
            tour.erase(tour.begin() + static_cast<std::ptrdiff_t>(from));
            tour.insert(tour.begin() + static_cast<std::ptrdiff_t>(to), visit);
        }
        return tour;
    }
}
