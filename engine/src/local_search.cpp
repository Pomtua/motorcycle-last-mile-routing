#include "router/local_search.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "router/insertion.hpp"
#include "router/zones.hpp"

namespace router
{
    namespace
    {
        constexpr double kEpsilon = 1e-6;
        constexpr double kTimeTolerance = 1e-6;

        struct MoveStats
        {
            double totalMs = 0.0;
            int calls = 0;
            int accepted = 0;
        };

        struct Profile
        {
            MoveStats relocate;
            MoveStats swap;
            MoveStats twoOptStar;
            MoveStats eliminate;
            long long bestInsertionCalls = 0;
            long long eliminateTrialCopies = 0;
        };

        Profile &profile()
        {
            static Profile p;
            return p;
        }

        bool profilingEnabled()
        {
            static const bool enabled = (std::getenv("LS_PROFILE") != nullptr);
            return enabled;
        }

        InsertionCandidate countedBestInsertion(const Instance &inst, const Route &route,
                                                const Visit &visit)
        {
            if (profilingEnabled())
            {
                ++profile().bestInsertionCalls;
            }
            return bestInsertion(inst, route, visit);
        }

        template <typename Fn>
        bool timedTry(MoveStats &stats, Fn &&fn)
        {
            if (!profilingEnabled())
            {
                return fn();
            }
            const auto t0 = std::chrono::steady_clock::now();
            const bool found = fn();
            stats.totalMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                    .count();
            ++stats.calls;
            if (found)
            {
                ++stats.accepted;
            }
            return found;
        }

        void printProfile()
        {
            const Profile &p = profile();
            auto row = [](const char *name, const MoveStats &s)
            {
                std::cerr << name << ": calls=" << s.calls << " accepted=" << s.accepted
                          << " totalMs=" << s.totalMs
                          << " avgMs=" << (s.calls ? s.totalMs / s.calls : 0.0) << "\n";
            };
            std::cerr << "--- local search profile ---\n";
            row("relocate ", p.relocate);
            row("swap     ", p.swap);
            row("2-opt*   ", p.twoOptStar);
            row("eliminate", p.eliminate);
            std::cerr << "bestInsertion calls:    " << p.bestInsertionCalls << "\n";
            std::cerr << "eliminate trial copies: " << p.eliminateTrialCopies << "\n";
        }

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

        struct SearchObjective
        {
            const std::vector<int> *zoneOf = nullptr;
            double zonePenalty = 0.0;
        };

        bool zoneObjectiveEnabled(const SearchObjective &objective)
        {
            return objective.zoneOf != nullptr &&
                   objective.zonePenalty > 0.0;
        }

        double stopsZoneCost(
            const Stops &stops,
            const SearchObjective &objective)
        {
            if (!zoneObjectiveEnabled(objective))
            {
                return 0.0;
            }

            std::set<int> zones;
            for (const Visit &stop : stops)
            {
                if (stop.nodeIndex <= 0 ||
                    static_cast<std::size_t>(stop.nodeIndex) >=
                        objective.zoneOf->size() ||
                    (*objective.zoneOf)
                            [static_cast<std::size_t>(stop.nodeIndex)] < 0)
                {
                    throw std::invalid_argument(
                        "localSearch: invalid zone assignment");
                }
                zones.insert(
                    (*objective.zoneOf)
                        [static_cast<std::size_t>(stop.nodeIndex)]);
            }

            return objective.zonePenalty *
                   static_cast<double>(
                       std::max(0, static_cast<int>(zones.size()) - 1));
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

        double totalScore(
            const Instance &inst,
            const Solution &sol,
            const SearchObjective &objective)
        {
            double score = totalDistance(inst, sol);
            if (objective.zoneOf != nullptr && objective.zonePenalty > 0.0)
            {
                score += computeRouteZoneCost(
                    sol, *objective.zoneOf, objective.zonePenalty);
            }
            return score;
        }

        struct ChunkRef
        {
            int nodeIndex = 0;
            int chunkIdx = 0;
        };

        bool findChunk(const Solution &sol, const ChunkRef &ref,
                       std::size_t &routeOut, std::size_t &posOut)
        {
            for (std::size_t r = 0; r < sol.routes.size(); ++r)
            {
                const Stops &stops = sol.routes[r].stops;
                for (std::size_t p = 0; p < stops.size(); ++p)
                {
                    if (stops[p].nodeIndex == ref.nodeIndex &&
                        stops[p].chunkIdx == ref.chunkIdx)
                    {
                        routeOut = r;
                        posOut = p;
                        return true;
                    }
                }
            }
            return false;
        }

        bool relocatePass(
            const Instance &inst,
            Solution &sol,
            const SearchObjective &objective)
        {
            std::vector<ChunkRef> refs;
            for (const Route &route : sol.routes)
            {
                for (const Visit &s : route.stops)
                {
                    refs.push_back({s.nodeIndex, s.chunkIdx});
                }
            }

            bool improved = false;

            for (const ChunkRef &ref : refs)
            {
                std::size_t r = 0;
                std::size_t p = 0;
                if (!findChunk(sol, ref, r, p))
                {
                    continue;
                }

                const Visit chunk = sol.routes[r].stops[p];
                const double gain = removalGain(inst, sol.routes[r], p);

                bool applied = false;

                for (std::size_t r2 = 0; r2 < sol.routes.size() && !applied; ++r2)
                {
                    if (r2 == r)
                    {
                        continue;
                    }

                    const InsertionCandidate cand =
                        countedBestInsertion(inst, sol.routes[r2], chunk);
                    if (!cand.feasible)
                    {
                        continue;
                    }

                    const double distanceDelta = cand.cost - gain;
                    if (!zoneObjectiveEnabled(objective) &&
                        distanceDelta >= -kEpsilon)
                    {
                        continue;
                    }

                    Stops shortened = sol.routes[r].stops;
                    shortened.erase(
                        shortened.begin() + static_cast<std::ptrdiff_t>(p));
                    Stops expanded = sol.routes[r2].stops;
                    expanded.insert(
                        expanded.begin() +
                            static_cast<std::ptrdiff_t>(cand.position),
                        chunk);

                    const double zoneDelta =
                        stopsZoneCost(shortened, objective) +
                        stopsZoneCost(expanded, objective) -
                        stopsZoneCost(sol.routes[r].stops, objective) -
                        stopsZoneCost(sol.routes[r2].stops, objective);
                    if (distanceDelta + zoneDelta >= -kEpsilon)
                    {
                        continue;
                    }

                    sol.routes[r].stops = std::move(shortened);
                    sol.routes[r2].stops = std::move(expanded);
                    dropEmptyRoutes(sol);
                    applied = true;
                }

                if (applied)
                {
                    improved = true;
                    continue;
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
                    improved = true;
                    break;
                }
            }

            return improved;
        }

        bool swapPass(
            const Instance &inst,
            Solution &sol,
            const SearchObjective &objective)
        {
            bool improved = false;

            for (std::size_t r1 = 0; r1 < sol.routes.size(); ++r1)
            {
                for (std::size_t r2 = r1 + 1; r2 < sol.routes.size(); ++r2)
                {
                    for (std::size_t p = 0; p < sol.routes[r1].stops.size(); ++p)
                    {
                        for (std::size_t q = 0; q < sol.routes[r2].stops.size(); ++q)
                        {
                            const Stops &a = sol.routes[r1].stops;
                            const Stops &b = sol.routes[r2].stops;

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

                            if (!zoneObjectiveEnabled(objective) &&
                                delta >= -kEpsilon)
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

                            const double zoneDelta =
                                stopsZoneCost(newA, objective) +
                                stopsZoneCost(newB, objective) -
                                stopsZoneCost(a, objective) -
                                stopsZoneCost(b, objective);
                            if (delta + zoneDelta >= -kEpsilon)
                            {
                                continue;
                            }

                            sol.routes[r1].stops = std::move(newA);
                            sol.routes[r2].stops = std::move(newB);
                            improved = true;
                        }
                    }
                }
            }

            return improved;
        }

        bool twoOptStarPass(
            const Instance &inst,
            Solution &sol,
            const SearchObjective &objective)
        {
            bool improved = false;

            for (std::size_t r1 = 0; r1 < sol.routes.size(); ++r1)
            {
                for (std::size_t r2 = r1 + 1; r2 < sol.routes.size(); ++r2)
                {
                    bool appliedHere = false;

                    const Stops &a = sol.routes[r1].stops;
                    const Stops &b = sol.routes[r2].stops;

                    for (std::size_t i = 0; i <= a.size() && !appliedHere; ++i)
                    {
                        for (std::size_t j = 0; j <= b.size() && !appliedHere; ++j)
                        {
                            const int lastA = nodeBefore(a, i);
                            const int firstA = nodeAt(a, i);
                            const int lastB = nodeBefore(b, j);
                            const int firstB = nodeAt(b, j);

                            const double delta =
                                dist(inst, lastA, firstB) + dist(inst, lastB, firstA) -
                                dist(inst, lastA, firstA) - dist(inst, lastB, firstB);

                            if (!zoneObjectiveEnabled(objective) &&
                                delta >= -kEpsilon)
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

                            const double zoneDelta =
                                stopsZoneCost(newA, objective) +
                                stopsZoneCost(newB, objective) -
                                stopsZoneCost(a, objective) -
                                stopsZoneCost(b, objective);
                            if (delta + zoneDelta >= -kEpsilon)
                            {
                                continue;
                            }

                            sol.routes[r1].stops = std::move(newA);
                            sol.routes[r2].stops = std::move(newB);
                            appliedHere = true;
                            improved = true;
                        }
                    }
                }
            }

            dropEmptyRoutes(sol);
            return improved;
        }

        bool eliminatePass(
            const Instance &inst,
            Solution &sol,
            const SearchObjective &objective)
        {
            if (sol.routes.size() < 2)
            {
                return false;
            }

            const bool overFleet = sol.routes.size() > static_cast<std::size_t>(inst.fleet.size);
            const double before = totalScore(inst, sol, objective);

            for (std::size_t r = 0; r < sol.routes.size(); ++r)
            {
                Solution trial = sol;
                if (profilingEnabled())
                {
                    ++profile().eliminateTrialCopies;
                }
                const Stops chunks = trial.routes[r].stops;
                trial.routes[r].stops.clear();

                bool placedAll = true;
                for (const Visit &chunk : chunks)
                {
                    std::size_t bestRoute = trial.routes.size();
                    InsertionCandidate bestCand;
                    double bestScore = 0.0;

                    for (std::size_t r2 = 0; r2 < trial.routes.size(); ++r2)
                    {
                        if (r2 == r)
                        {
                            continue;
                        }

                        const InsertionCandidate cand =
                            countedBestInsertion(inst, trial.routes[r2], chunk);
                        if (!cand.feasible)
                        {
                            continue;
                        }

                        double candidateScore = cand.cost;
                        if (zoneObjectiveEnabled(objective))
                        {
                            Stops expanded = trial.routes[r2].stops;
                            expanded.insert(
                                expanded.begin() +
                                    static_cast<std::ptrdiff_t>(cand.position),
                                chunk);
                            candidateScore +=
                                stopsZoneCost(expanded, objective) -
                                stopsZoneCost(
                                    trial.routes[r2].stops, objective);
                        }

                        if (bestRoute == trial.routes.size() ||
                            candidateScore < bestScore)
                        {
                            bestRoute = r2;
                            bestCand = cand;
                            bestScore = candidateScore;
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

                if (overFleet ||
                    totalScore(inst, trial, objective) - before < -kEpsilon)
                {
                    sol = std::move(trial);
                    return true;
                }
            }

            return false;
        }

        Solution runLocalSearch(
            const Instance &inst,
            Solution sol,
            const SearchObjective &objective)
        {
            profile() = Profile{};

            bool improved = true;
            while (improved)
            {
                improved = false;
                improved |= timedTry(
                    profile().relocate,
                    [&] { return relocatePass(inst, sol, objective); });
                improved |= timedTry(
                    profile().swap,
                    [&] { return swapPass(inst, sol, objective); });
                improved |= timedTry(
                    profile().twoOptStar,
                    [&] { return twoOptStarPass(inst, sol, objective); });
                improved |= timedTry(
                    profile().eliminate,
                    [&] { return eliminatePass(inst, sol, objective); });
            }

            if (profilingEnabled())
            {
                printProfile();
            }

            return sol;
        }

    }

    Solution localSearch(const Instance &inst, Solution sol)
    {
        return runLocalSearch(inst, std::move(sol), SearchObjective{});
    }

    Solution localSearch(
        const Instance &inst,
        Solution sol,
        const std::vector<int> &zoneOf,
        double zonePenalty)
    {
        if (zoneOf.size() != inst.nodes.size())
        {
            throw std::invalid_argument(
                "localSearch: zone assignment size does not match instance");
        }
        if (!std::isfinite(zonePenalty) || zonePenalty < 0.0)
        {
            throw std::invalid_argument(
                "localSearch: zone penalty must be finite and non-negative");
        }

        return runLocalSearch(
            inst,
            std::move(sol),
            SearchObjective{&zoneOf, zonePenalty});
    }

}
