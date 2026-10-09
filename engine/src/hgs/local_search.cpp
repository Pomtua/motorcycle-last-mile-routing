#include "router/hgs/local_search.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace router::hgs
{
    LocalSearch::LocalSearch(const ProblemData &data, std::vector<std::unique_ptr<Move>> moves)
        : data_(data),
          moves_(std::move(moves)),
          routes_(data),
          lastTested_(data.visitCount() + 1, 0)
    {
        if (moves_.empty())
        {
            throw std::invalid_argument("local search needs at least one move");
        }
        order_.resize(data.visitCount());
    }

    bool LocalSearch::tryMoves(SearchNode &u, SearchNode &v)
    {
        for (const auto &move : moves_)
        {
            if (move->tryApply(u, v, routes_))
            {
                if (checkInvariants_)
                {
                    routes_.checkInvariants();
                }
                return true;
            }
        }
        return false;
    }

    void LocalSearch::run(Individual &individual, const Penalties &penalties, Rng &rng, Deadline &deadline)
    {
        ScopedTimer timer(stats_.time);
        ++stats_.runs;
        routes_.load(individual, penalties);
        if (checkInvariants_)
        {
            routes_.checkInvariants();
        }
        std::iota(order_.begin(), order_.end(), 1);
        std::shuffle(order_.begin(), order_.end(), rng);

        bool improved = true;
        bool interrupted = false;
        for (std::size_t pass = 0; improved && !interrupted; ++pass)
        {
            ++stats_.passes;
            improved = false;
            for (int visit : order_)
            {
                if (deadline.expiredEvery(kLocalSearchDeadlineInterval))
                {
                    ++stats_.interruptedByDeadline;
                    interrupted = true;
                    break;
                }
                const std::uint64_t testedAt = lastTested_[static_cast<std::size_t>(visit)];
                lastTested_[static_cast<std::size_t>(visit)] = routes_.modificationCount();

                for (int neighbour : data_.neighbours(visit))
                {
                    SearchNode &u = routes_.node(visit);
                    SearchNode &v = routes_.node(neighbour);
                    if (pass > 0 &&
                        routes_.route(u.route).lastModified <= testedAt &&
                        routes_.route(v.route).lastModified <= testedAt)
                    {
                        ++stats_.pairsSkipped;
                        continue;
                    }
                    improved = tryMoves(u, v) || improved;
                }

                if (SearchRoute *empty = routes_.firstEmptyRoute())
                {
                    improved = tryMoves(routes_.node(visit), empty->start) || improved;
                }
            }
        }

        individual = makeIndividual(data_, routes_.exportRoutes());
    }
}
