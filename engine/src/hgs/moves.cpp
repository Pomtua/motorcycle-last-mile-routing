#include "router/hgs/moves.hpp"

#include <stdexcept>

namespace router::hgs
{
    std::optional<double> Relocate::evaluate(const SearchNode &u, const SearchNode &target, const SearchRoutes &routes)
    {
        if (&target == &u || target.next == &u || target.next == nullptr)
        {
            return std::nullopt;
        }

        const ProblemData &data = routes.data();
        const SearchRoute &from = routes.route(u.route);
        const RouteSummary moved = visitSummary(data, u.visit);

        if (u.route != target.route)
        {
            const SearchRoute &to = routes.route(target.route);
            const RouteSummary fromAfter = concat(data, u.prev->prefix, u.next->suffix);
            const RouteSummary toAfter = concat(data, concat(data, target.prefix, moved), target.next->suffix);
            return routes.cost(fromAfter) + routes.cost(toAfter) - from.cost - to.cost;
        }

        RouteSummary after;
        if (target.position < u.position)
        {
            const RouteSummary skipped = routes.summaryBetween(*target.next, *u.prev);
            after = concat(data, concat(data, concat(data, target.prefix, moved), skipped), u.next->suffix);
        }
        else
        {
            const RouteSummary skipped = routes.summaryBetween(*u.next, target);
            after = concat(data, concat(data, concat(data, u.prev->prefix, skipped), moved), target.next->suffix);
        }
        return routes.cost(after) - from.cost;
    }

    void Relocate::apply(SearchNode &u, SearchNode &target, SearchRoutes &routes)
    {
        const int fromRoute = u.route;
        routes.moveAfter(u, target);
        routes.update(routes.route(target.route));
        if (fromRoute != target.route)
        {
            routes.update(routes.route(fromRoute));
        }
    }

    bool Relocate::tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes)
    {
        SearchNode *bestTarget = nullptr;
        double bestDelta = -kImprovementEpsilon;
        for (SearchNode *target : {&v, v.isDepot() ? nullptr : v.prev})
        {
            if (target == nullptr)
            {
                continue;
            }
            const std::optional<double> delta = evaluate(u, *target, routes);
            if (!delta)
            {
                continue;
            }
            ++stats_.evaluated;
            if (*delta < bestDelta)
            {
                bestDelta = *delta;
                bestTarget = target;
            }
        }
        if (bestTarget == nullptr)
        {
            return false;
        }

        ++stats_.improving;
        if (u.route != v.route && routes.hasSiblingInRoute(u.visit, v.route))
        {
            ++stats_.rejectedByConflict;
            return false;
        }
        apply(u, *bestTarget, routes);
        ++stats_.applied;
        return true;
    }

    std::vector<std::unique_ptr<Move>> makeMoves(const std::vector<std::string> &names)
    {
        if (names.empty())
        {
            throw std::invalid_argument("local search needs at least one move");
        }
        std::vector<std::unique_ptr<Move>> moves;
        for (const std::string &name : names)
        {
            if (name == "relocate")
            {
                moves.push_back(std::make_unique<Relocate>());
            }
            else
            {
                throw std::invalid_argument("unknown local search move: " + name);
            }
        }
        return moves;
    }
}
