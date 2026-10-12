#include "router/hgs/local_search.hpp"

#include <algorithm>
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
        hasSiblingMoves_ = hasMoves(MoveScope::Sibling);
        hasSingleMoves_ = hasMoves(MoveScope::Single);
    }

    bool LocalSearch::hasMoves(MoveScope scope) const
    {
        return std::any_of(moves_.begin(), moves_.end(), [scope](const auto &move) { return move->scope() == scope; });
    }

    bool LocalSearch::routesUnchangedSince(const SearchNode &u, std::uint64_t testedAt)
    {
        const auto unchanged = [&](int id) { return routes_.route(routes_.node(id).route).lastModified <= testedAt; };
        if (!unchanged(u.id))
        {
            return false;
        }
        for (int neighbour : data_.neighbours(u.customer))
        {
            if (!std::all_of(routes_.nodesOf(neighbour).begin(), routes_.nodesOf(neighbour).end(), unchanged))
            {
                return false;
            }
        }
        if (!std::all_of(routes_.nodesOf(u.customer).begin(), routes_.nodesOf(u.customer).end(), unchanged))
        {
            return false;
        }
        const SearchRoute *empty = routes_.firstEmptyRoute();
        return empty == nullptr || empty->lastModified <= testedAt;
    }

    void LocalSearch::rebuildOrder(Rng &rng)
    {
        order_ = routes_.activeNodes();
        std::sort(order_.begin(), order_.end());
        std::shuffle(order_.begin(), order_.end(), rng);
        orderVersion_ = routes_.structureVersion();
    }

    bool LocalSearch::tryMoves(SearchNode &u, SearchNode &v, MoveScope scope)
    {
        for (const auto &move : moves_)
        {
            if (move->scope() != scope || u.isDepot())
            {
                continue;
            }
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
        rebuildOrder(rng);

        bool improved = true;
        bool interrupted = false;
        for (std::size_t pass = 0; improved && !interrupted; ++pass)
        {
            ++stats_.passes;
            improved = false;
            if (routes_.structureVersion() != orderVersion_)
            {
                rebuildOrder(rng);
            }
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
                        improved = tryMoves(u, v, MoveScope::Pair) || improved;
                    }
                }

                if (hasSiblingMoves_ && !routes_.node(id).isDepot())
                {
                    candidates_ = routes_.nodesOf(routes_.node(id).customer);
                    for (int sibling : candidates_)
                    {
                        SearchNode &u = routes_.node(id);
                        SearchNode &s = routes_.node(sibling);
                        if (sibling == id || u.isDepot() || s.isDepot() || s.customer != u.customer)
                        {
                            continue;
                        }
                        if (pass > 0 &&
                            routes_.route(u.route).lastModified <= testedAt &&
                            routes_.route(s.route).lastModified <= testedAt)
                        {
                            ++stats_.siblingsSkipped;
                            continue;
                        }
                        improved = tryMoves(u, s, MoveScope::Sibling) || improved;
                    }
                }

                SearchRoute *empty = routes_.firstEmptyRoute();
                if (empty != nullptr && !routes_.node(id).isDepot())
                {
                    improved = tryMoves(routes_.node(id), empty->start, MoveScope::Pair) || improved;
                }

                if (hasSingleMoves_ && !routes_.node(id).isDepot())
                {
                    if (pass > 0 && routesUnchangedSince(routes_.node(id), testedAt))
                    {
                        ++stats_.singlesSkipped;
                    }
                    else
                    {
                        improved = tryMoves(routes_.node(id), routes_.node(id), MoveScope::Single) || improved;
                    }
                }
            }
        }

        individual = makeIndividual(data_, routes_.exportRoutes());
    }
}
