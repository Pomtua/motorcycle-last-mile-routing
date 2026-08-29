#include "router/local_search.hpp"

#include <cstddef>
#include <utility>
#include <vector>

#include "router/insertion.hpp"

namespace router
{
    namespace
    {
        constexpr double kEpsilon = 1e-6;
        constexpr double kTimeTolerance = 1e-6;

        using Stops = std::vector<Visit>;

        int nodeAt(const Stops &stops, std::size_t k)
        {
            return k < stops.size() ? stops[k].nodeIndex : 0;
        }

        int nodeBefore(const Stops &stops, std::size_t k)
        {
            return k > 0 ? stops[k - 1].nodeIndex : 0;
        }

        double dist(const Instance &inst, int from, int to)
        {
            return inst.distanceMatrix[static_cast<std::size_t>(from)]
                                      [static_cast<std::size_t>(to)];
        }

        double stopsDistance(const Instance &inst, const Stops &stops)
        {
            if (stops.empty())
            {
                return 0.0;
            }

            double total = 0.0;
            int current = 0;
            for (const Visit &s : stops)
            {
                total += dist(inst, current, s.nodeIndex);
                current = s.nodeIndex;
            }
            return total + dist(inst, current, 0);
        }

        bool stopsFeasible(const Instance &inst, const Stops &stops)
        {
            double weight = 0.0;
            double volume = 0.0;
            for (const Visit &s : stops)
            {
                weight += s.weight;
                volume += s.volume;
            }
            if (weight > inst.fleet.weightCapacity + 1e-6)
            {
                return false;
            }
            if (volume > inst.fleet.volumeCapacity + 1e-9)
            {
                return false;
            }

            for (std::size_t i = 0; i < stops.size(); ++i)
            {
                for (std::size_t j = i + 1; j < stops.size(); ++j)
                {
                    if (stops[i].nodeIndex == stops[j].nodeIndex)
                    {
                        return false;
                    }
                }
            }

            double clock = 0.0;
            int current = 0;
            for (const Visit &s : stops)
            {
                const Node &node = inst.nodes[static_cast<std::size_t>(s.nodeIndex)];

                double arrival = clock + inst.durationMatrix[static_cast<std::size_t>(current)]
                                                            [static_cast<std::size_t>(s.nodeIndex)];
                if (arrival < node.twStart)
                {
                    arrival = node.twStart;
                }
                if (arrival > node.twEnd + kTimeTolerance)
                {
                    return false;
                }
                clock = arrival + node.serviceTime;
                current = s.nodeIndex;
            }

            const double back = inst.durationMatrix[static_cast<std::size_t>(current)][0];
            return clock + back <= inst.horizon + kTimeTolerance;
        }

        void dropEmptyRoutes(Solution &sol)
        {
            std::size_t write = 0;
            for (std::size_t r = 0; r < sol.routes.size(); ++r)
            {
                if (sol.routes[r].stops.empty())
                {
                    continue;
                }

                if (write != r)
                {
                    sol.routes[write] = std::move(sol.routes[r]);
                }
                ++write;
            }
            sol.routes.resize(write);
        }

        double totalDistance(const Instance &inst, const Solution &sol)
        {
            double total = 0.0;
            for (const Route &route : sol.routes)
            {
                total += stopsDistance(inst, route.stops);
            }
            return total;
        }

        bool tryRelocate(const Instance &inst, Solution &sol)
        {
            for (std::size_t r = 0; r < sol.routes.size(); ++r)
            {
                for (std::size_t p = 0; p < sol.routes[r].stops.size(); ++p)
                {
                    const Visit chunk = sol.routes[r].stops[p];
                    const double gain = removalGain(inst, sol.routes[r], p);

                    for (std::size_t r2 = 0; r2 < sol.routes.size(); ++r2)
                    {
                        if (r2 == r)
                        {
                            continue;
                        }

                        const InsertionCandidate cand =
                            bestInsertion(inst, sol.routes[r2], chunk);
                        if (!cand.feasible || cand.cost - gain >= -kEpsilon)
                        {
                            continue;
                        }

                        sol.routes[r].stops.erase(sol.routes[r].stops.begin() +
                                                  static_cast<std::ptrdiff_t>(p));
                        sol.routes[r2].stops.insert(
                            sol.routes[r2].stops.begin() +
                                static_cast<std::ptrdiff_t>(cand.position),
                            chunk);
                        dropEmptyRoutes(sol);
                        return true;
                    }

                    Stops shortened = sol.routes[r].stops;
                    shortened.erase(shortened.begin() + static_cast<std::ptrdiff_t>(p));

                    for (std::size_t q = 0; q <= shortened.size(); ++q)
                    {
                        if (q == p)
                        {
                            continue;
                        }

                        const int prevQ = nodeBefore(shortened, q);
                        const int nextQ = nodeAt(shortened, q);

                        const double insertCost = dist(inst, prevQ, chunk.nodeIndex) +
                                                  dist(inst, chunk.nodeIndex, nextQ) -
                                                  dist(inst, prevQ, nextQ);

                        if (insertCost - gain >= -kEpsilon)
                        {
                            continue;
                        }

                        Stops candidate = shortened;
                        candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(q),
                                         chunk);

                        if (!stopsFeasible(inst, candidate))
                        {
                            continue;
                        }

                        sol.routes[r].stops = std::move(candidate);
                        return true;
                    }
                }
            }

            return false;
        }

        bool trySwap(const Instance &inst, Solution &sol)
        {
            for (std::size_t r1 = 0; r1 < sol.routes.size(); ++r1)
            {
                for (std::size_t r2 = r1 + 1; r2 < sol.routes.size(); ++r2)
                {
                    const Stops &a = sol.routes[r1].stops;
                    const Stops &b = sol.routes[r2].stops;

                    for (std::size_t p = 0; p < a.size(); ++p)
                    {
                        for (std::size_t q = 0; q < b.size(); ++q)
                        {
                            const int ap = a[p].nodeIndex;
                            const int bq = b[q].nodeIndex;

                            const int predA = nodeBefore(a, p);
                            const int succA = nodeAt(a, p + 1);
                            const int predB = nodeBefore(b, q);
                            const int succB = nodeAt(b, q + 1);

                            const double delta =
                                dist(inst, predA, bq) + dist(inst, bq, succA) -
                                dist(inst, predA, ap) - dist(inst, ap, succA) +
                                dist(inst, predB, ap) + dist(inst, ap, succB) -
                                dist(inst, predB, bq) - dist(inst, bq, succB);

                            if (delta >= -kEpsilon)
                            {
                                continue;
                            }

                            Stops newA = a;
                            Stops newB = b;
                            std::swap(newA[p], newB[q]);

                            if (!stopsFeasible(inst, newA) || !stopsFeasible(inst, newB))
                            {
                                continue;
                            }

                            sol.routes[r1].stops = std::move(newA);
                            sol.routes[r2].stops = std::move(newB);
                            return true;
                        }
                    }
                }
            }

            return false;
        }

        bool tryTwoOptStar(const Instance &inst, Solution &sol)
        {
            for (std::size_t r1 = 0; r1 < sol.routes.size(); ++r1)
            {
                for (std::size_t r2 = r1 + 1; r2 < sol.routes.size(); ++r2)
                {
                    const Stops &a = sol.routes[r1].stops;
                    const Stops &b = sol.routes[r2].stops;

                    for (std::size_t i = 0; i <= a.size(); ++i)
                    {
                        for (std::size_t j = 0; j <= b.size(); ++j)
                        {
                            const int lastA = nodeBefore(a, i);
                            const int firstA = nodeAt(a, i);
                            const int lastB = nodeBefore(b, j);
                            const int firstB = nodeAt(b, j);

                            const double delta =
                                dist(inst, lastA, firstB) + dist(inst, lastB, firstA) -
                                dist(inst, lastA, firstA) - dist(inst, lastB, firstB);

                            if (delta >= -kEpsilon)
                            {
                                continue;
                            }

                            Stops newA(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(i));
                            newA.insert(newA.end(),
                                        b.begin() + static_cast<std::ptrdiff_t>(j), b.end());

                            Stops newB(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(j));
                            newB.insert(newB.end(),
                                        a.begin() + static_cast<std::ptrdiff_t>(i), a.end());

                            if (!stopsFeasible(inst, newA) || !stopsFeasible(inst, newB))
                            {
                                continue;
                            }

                            sol.routes[r1].stops = std::move(newA);
                            sol.routes[r2].stops = std::move(newB);
                            dropEmptyRoutes(sol);
                            return true;
                        }
                    }
                }
            }

            return false;
        }

        bool tryEliminateRoute(const Instance &inst, Solution &sol)
        {
            if (sol.routes.size() < 2)
            {
                return false;
            }

            const double before = totalDistance(inst, sol);

            for (std::size_t r = 0; r < sol.routes.size(); ++r)
            {
                Solution trial = sol;
                const Stops chunks = trial.routes[r].stops;
                trial.routes[r].stops.clear();

                bool placedAll = true;
                for (const Visit &chunk : chunks)
                {
                    std::size_t bestRoute = trial.routes.size();
                    InsertionCandidate bestCand;

                    for (std::size_t r2 = 0; r2 < trial.routes.size(); ++r2)
                    {
                        if (r2 == r)
                        {
                            continue;
                        }

                        const InsertionCandidate cand =
                            bestInsertion(inst, trial.routes[r2], chunk);
                        if (!cand.feasible)
                        {
                            continue;
                        }
                        if (bestRoute == trial.routes.size() || cand.cost < bestCand.cost)
                        {
                            bestRoute = r2;
                            bestCand = cand;
                        }
                    }

                    if (bestRoute == trial.routes.size())
                    {
                        placedAll = false;
                        break;
                    }

                    trial.routes[bestRoute].stops.insert(
                        trial.routes[bestRoute].stops.begin() +
                            static_cast<std::ptrdiff_t>(bestCand.position),
                        chunk);
                }

                if (!placedAll)
                {
                    continue;
                }

                dropEmptyRoutes(trial);

                if (totalDistance(inst, trial) - before < -kEpsilon)
                {
                    sol = std::move(trial);
                    return true;
                }
            }

            return false;
        }

    }

    Solution localSearch(const Instance &inst, Solution sol)
    {
        while (true)
        {
            if (tryRelocate(inst, sol))
            {
                continue;
            }
            if (trySwap(inst, sol))
            {
                continue;
            }
            if (tryTwoOptStar(inst, sol))
            {
                continue;
            }
            if (tryEliminateRoute(inst, sol))
            {
                continue;
            }
            break;
        }

        return sol;
    }

}
