#include "router/hgs/individual.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

#include "router/hgs/route_summary.hpp"

namespace router::hgs
{
    bool isCompletePermutation(const std::vector<int> &giantTour, std::size_t visitCount)
    {
        if (giantTour.size() != visitCount)
        {
            return false;
        }
        std::vector<bool> seen(visitCount + 1, false);
        for (int visit : giantTour)
        {
            if (visit < 1 || static_cast<std::size_t>(visit) > visitCount ||
                seen[static_cast<std::size_t>(visit)])
            {
                return false;
            }
            seen[static_cast<std::size_t>(visit)] = true;
        }
        return true;
    }

    Individual makeIndividual(const ProblemData &data, std::vector<std::vector<int>> routes)
    {
        routes.erase(
            std::remove_if(routes.begin(), routes.end(), [](const auto &route) { return route.empty(); }),
            routes.end());
        if (routes.size() > static_cast<std::size_t>(data.fleetSize()))
        {
            throw std::invalid_argument("individual uses more routes than the fleet");
        }

        Individual individual;
        individual.giantTour.reserve(data.visitCount());
        for (const auto &route : routes)
        {
            individual.giantTour.insert(individual.giantTour.end(), route.begin(), route.end());
        }
        if (!isCompletePermutation(individual.giantTour, data.visitCount()))
        {
            throw std::invalid_argument("individual routes must serve every visit exactly once");
        }

        std::vector<std::size_t> routeOfVisit(data.visitCount() + 1, routes.size());
        for (std::size_t routeIndex = 0; routeIndex < routes.size(); ++routeIndex)
        {
            for (int visit : routes[routeIndex])
            {
                routeOfVisit[static_cast<std::size_t>(visit)] = routeIndex;
            }
        }
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            for (int sibling : data.siblings(visit))
            {
                if (routeOfVisit[static_cast<std::size_t>(sibling)] == routeOfVisit[static_cast<std::size_t>(visit)])
                {
                    throw std::invalid_argument("a route cannot serve two chunks of the same customer");
                }
            }
        }

        individual.successor.assign(data.visitCount() + 1, ProblemData::kDepot);
        individual.predecessor.assign(data.visitCount() + 1, ProblemData::kDepot);
        for (const auto &route : routes)
        {
            for (std::size_t position = 0; position < route.size(); ++position)
            {
                const auto visit = static_cast<std::size_t>(route[position]);
                individual.predecessor[visit] = position == 0 ? ProblemData::kDepot : route[position - 1];
                individual.successor[visit] = position + 1 == route.size() ? ProblemData::kDepot : route[position + 1];
            }
            individual.cost += routeCost(data, summarizeRoute(data, route));
        }
        individual.routes = std::move(routes);
        return individual;
    }

    Individual individualFromSolution(const ProblemData &data, const Solution &solution)
    {
        std::map<std::pair<int, int>, int> visitOfChunk;
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            const Visit &chunk = data.catalogVisit(visit);
            visitOfChunk[{chunk.nodeIndex, chunk.chunkIdx}] = visit;
        }

        std::vector<std::vector<int>> routes;
        routes.reserve(solution.routes.size());
        for (const Route &route : solution.routes)
        {
            std::vector<int> visits;
            visits.reserve(route.stops.size());
            for (const Visit &stop : route.stops)
            {
                const auto found = visitOfChunk.find({stop.nodeIndex, stop.chunkIdx});
                if (found == visitOfChunk.end())
                {
                    throw std::invalid_argument("solution contains a chunk outside the visit catalog");
                }
                visits.push_back(found->second);
            }
            routes.push_back(std::move(visits));
        }
        return makeIndividual(data, std::move(routes));
    }

    Solution toSolution(const ProblemData &data, const Individual &individual)
    {
        Solution solution;
        solution.routes.reserve(individual.routes.size());
        for (const auto &route : individual.routes)
        {
            Route exported;
            exported.stops.reserve(route.size());
            for (int visit : route)
            {
                exported.stops.push_back(data.catalogVisit(visit));
            }
            solution.routes.push_back(std::move(exported));
        }
        return solution;
    }
}
