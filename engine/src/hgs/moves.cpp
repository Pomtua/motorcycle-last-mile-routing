#include "router/hgs/moves.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

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

    bool Relocate::conflicts(const SearchNode &u, const SearchNode &target, const SearchRoutes &routes)
    {
        return u.route != target.route && routes.hasSiblingInRoute(u.visit, target.route);
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
        if (conflicts(u, *bestTarget, routes))
        {
            ++stats_.rejectedByConflict;
            return false;
        }
        apply(u, *bestTarget, routes);
        ++stats_.applied;
        return true;
    }

    std::optional<double> Swap::evaluate(const SearchNode &u, const SearchNode &v, const SearchRoutes &routes)
    {
        if (&u == &v || u.isDepot() || v.isDepot())
        {
            return std::nullopt;
        }

        const ProblemData &data = routes.data();
        const RouteSummary uSummary = visitSummary(data, u.visit);
        const RouteSummary vSummary = visitSummary(data, v.visit);
        if (u.route != v.route)
        {
            const RouteSummary uRouteAfter = concat(data, concat(data, u.prev->prefix, vSummary), u.next->suffix);
            const RouteSummary vRouteAfter = concat(data, concat(data, v.prev->prefix, uSummary), v.next->suffix);
            return routes.cost(uRouteAfter) + routes.cost(vRouteAfter) -
                   routes.route(u.route).cost - routes.route(v.route).cost;
        }

        const SearchNode &first = u.position < v.position ? u : v;
        const SearchNode &second = u.position < v.position ? v : u;
        RouteSummary after = concat(data, first.prev->prefix, visitSummary(data, second.visit));
        if (first.next != &second)
        {
            after = concat(data, after, routes.summaryBetween(*first.next, *second.prev));
        }
        after = concat(data, concat(data, after, visitSummary(data, first.visit)), second.next->suffix);
        return routes.cost(after) - routes.route(u.route).cost;
    }

    bool Swap::conflicts(const SearchNode &u, const SearchNode &v, const SearchRoutes &routes)
    {
        return u.route != v.route &&
               (routes.hasSiblingInRoute(u.visit, v.route, v.visit) ||
                routes.hasSiblingInRoute(v.visit, u.route, u.visit));
    }

    void Swap::apply(SearchNode &u, SearchNode &v, SearchRoutes &routes)
    {
        const int uRoute = u.route;
        const int vRoute = v.route;
        routes.swapNodes(u, v);
        routes.update(routes.route(uRoute));
        if (vRoute != uRoute)
        {
            routes.update(routes.route(vRoute));
        }
    }

    bool Swap::tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes)
    {
        const std::optional<double> delta = evaluate(u, v, routes);
        if (!delta)
        {
            return false;
        }
        ++stats_.evaluated;
        if (*delta >= -kImprovementEpsilon)
        {
            return false;
        }
        ++stats_.improving;
        if (conflicts(u, v, routes))
        {
            ++stats_.rejectedByConflict;
            return false;
        }
        apply(u, v, routes);
        ++stats_.applied;
        return true;
    }

    std::optional<double> TwoOptStar::evaluate(const SearchNode &u, const SearchNode &cut, const SearchRoutes &routes)
    {
        if (u.isDepot() || u.route == cut.route || cut.next == nullptr)
        {
            return std::nullopt;
        }
        const ProblemData &data = routes.data();
        const RouteSummary uRouteAfter = concat(data, u.prefix, cut.next->suffix);
        const RouteSummary cutRouteAfter = concat(data, cut.prefix, u.next->suffix);
        return routes.cost(uRouteAfter) + routes.cost(cutRouteAfter) -
               routes.route(u.route).cost - routes.route(cut.route).cost;
    }

    bool TwoOptStar::conflicts(const SearchNode &u, const SearchNode &cut, const SearchRoutes &routes)
    {
        const auto tailMeetsHead = [&](const SearchNode &tailStart, int headRoute, int headLastPosition)
        {
            for (const SearchNode *moved = &tailStart; !moved->isDepot(); moved = moved->next)
            {
                for (int sibling : routes.data().siblings(moved->visit))
                {
                    const SearchNode &other = routes.node(sibling);
                    if (other.route == headRoute && other.position <= headLastPosition)
                    {
                        return true;
                    }
                }
            }
            return false;
        };
        return tailMeetsHead(*cut.next, u.route, u.position) || tailMeetsHead(*u.next, cut.route, cut.position);
    }

    void TwoOptStar::apply(SearchNode &u, SearchNode &cut, SearchRoutes &routes)
    {
        const int uRoute = u.route;
        const int cutRoute = cut.route;
        routes.exchangeTails(u, cut);
        routes.update(routes.route(uRoute));
        routes.update(routes.route(cutRoute));
    }

    bool TwoOptStar::tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes)
    {
        std::vector<std::pair<double, SearchNode *>> improving;
        for (SearchNode *cut : {&v, v.isDepot() ? nullptr : v.prev})
        {
            if (cut == nullptr)
            {
                continue;
            }
            const std::optional<double> delta = evaluate(u, *cut, routes);
            if (!delta)
            {
                continue;
            }
            ++stats_.evaluated;
            if (*delta < -kImprovementEpsilon)
            {
                improving.emplace_back(*delta, cut);
            }
        }
        std::sort(improving.begin(), improving.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[delta, cut] : improving)
        {
            ++stats_.improving;
            if (conflicts(u, *cut, routes))
            {
                ++stats_.rejectedByConflict;
                continue;
            }
            apply(u, *cut, routes);
            ++stats_.applied;
            return true;
        }
        return false;
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
            else if (name == "swap")
            {
                moves.push_back(std::make_unique<Swap>());
            }
            else if (name == "2-opt*")
            {
                moves.push_back(std::make_unique<TwoOptStar>());
            }
            else
            {
                throw std::invalid_argument("unknown local search move: " + name);
            }
        }
        return moves;
    }
}
