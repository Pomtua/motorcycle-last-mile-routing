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
          lastTested_(routes_.nodeCapacity() + 1, 0)
    {
        if (moves_.empty())
        {
            throw std::invalid_argument("local search needs at least one move");
        }
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
        order_ = routes_.activeNodes();
        std::sort(order_.begin(), order_.end());
        std::shuffle(order_.begin(), order_.end(), rng);

        bool improved = true;
        bool interrupted = false;
        for (std::size_t pass = 0; improved && !interrupted; ++pass)
        {
            ++stats_.passes;
            improved = false;
            for (int id : order_)
            {
                if (deadline.expiredEvery(kLocalSearchDeadlineInterval))
                {
                    ++stats_.interruptedByDeadline;
                    interrupted = true;
                    break;
                }
                if (routes_.node(id).isDepot())
                {
                    continue;
                }
                const std::uint64_t testedAt = lastTested_[static_cast<std::size_t>(id)];
                lastTested_[static_cast<std::size_t>(id)] = routes_.modificationCount();

                for (int neighbour : data_.neighbours(routes_.node(id).customer))
                {
                    candidates_ = routes_.nodesOf(neighbour);
                    for (int candidate : candidates_)
                    {
                        SearchNode &u = routes_.node(id);
                        SearchNode &v = routes_.node(candidate);
                        if (u.isDepot() || v.isDepot())
                        {
                            continue;
                        }
                        if (pass > 0 &&
                            routes_.route(u.route).lastModified <= testedAt &&
                            routes_.route(v.route).lastModified <= testedAt)
                        {
                            ++stats_.pairsSkipped;
                            continue;
                        }
                        improved = tryMoves(u, v) || improved;
                    }
                }

                SearchRoute *empty = routes_.firstEmptyRoute();
                if (empty != nullptr && !routes_.node(id).isDepot())
                {
                    improved = tryMoves(routes_.node(id), empty->start) || improved;
                }
            }
        }

        individual = makeIndividual(data_, routes_.exportRoutes());
    }
}
