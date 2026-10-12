#include "router/hgs/moves.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

namespace router::hgs
{
    namespace
    {
        RouteSummary withQuantity(const SearchRoutes &routes, const SearchNode &node, std::int64_t units)
        {
            const ProblemData &data = routes.data();
            return concat(data, concat(data, node.prev->prefix, visitSummary(data, {node.customer, units})), node.next->suffix);
        }

        RouteSummary insertedAfter(const SearchRoutes &routes, const SearchNode &target, const Delivery &delivery)
        {
            const ProblemData &data = routes.data();
            return concat(data, concat(data, target.prefix, visitSummary(data, delivery)), target.next->suffix);
        }

        RouteSummary withoutNode(const SearchRoutes &routes, const SearchNode &node)
        {
            return concat(routes.data(), node.prev->prefix, node.next->suffix);
        }

        std::vector<std::int64_t> candidateAmounts(
            std::initializer_list<std::int64_t> candidates,
            std::int64_t low,
            std::int64_t high)
        {
            std::vector<std::int64_t> amounts;
            if (high < low)
            {
                return amounts;
            }
            for (std::int64_t candidate : candidates)
            {
                if (candidate <= 0)
                {
                    continue;
                }
                const std::int64_t amount = std::clamp(candidate, low, high);
                if (std::find(amounts.begin(), amounts.end(), amount) == amounts.end())
                {
                    amounts.push_back(amount);
                }
            }
            return amounts;
        }
    }

    std::int64_t residualUnits(const ProblemData &data, const RouteSummary &route, int customer)
    {
        const CustomerData &target = data.customer(customer);
        const auto byWeight = static_cast<std::int64_t>(
            std::floor((data.weightCapacity() - route.weight) * kUnitsPerKilogram + 1e-9));
        if (target.volumePerUnit <= 0.0)
        {
            return byWeight;
        }
        const auto byVolume = static_cast<std::int64_t>(
            std::floor((data.volumeCapacity() - route.volume) / target.volumePerUnit + 1e-9));
        return std::min(byWeight, byVolume);
    }

    std::int64_t excessUnits(const ProblemData &data, const RouteSummary &route, int customer)
    {
        const CustomerData &target = data.customer(customer);
        auto excess = static_cast<std::int64_t>(
            std::ceil((route.weight - data.weightCapacity()) * kUnitsPerKilogram - 1e-9));
        if (target.volumePerUnit > 0.0)
        {
            excess = std::max(excess, static_cast<std::int64_t>(
                std::ceil((route.volume - data.volumeCapacity()) / target.volumePerUnit - 1e-9)));
        }
        return std::max<std::int64_t>(excess, 0);
    }

    std::optional<double> Relocate::evaluate(const SearchNode &u, const SearchNode &target, const SearchRoutes &routes)
    {
        if (&target == &u || target.next == &u || target.next == nullptr)
        {
            return std::nullopt;
        }

        const ProblemData &data = routes.data();
        const SearchRoute &from = routes.route(u.route);
        const RouteSummary moved = visitSummary(data, u.delivery());

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
        return u.route != target.route && routes.hasCustomerInRoute(u.customer, target.route, u.id);
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
        const RouteSummary uSummary = visitSummary(data, u.delivery());
        const RouteSummary vSummary = visitSummary(data, v.delivery());
        if (u.route != v.route)
        {
            const RouteSummary uRouteAfter = concat(data, concat(data, u.prev->prefix, vSummary), u.next->suffix);
            const RouteSummary vRouteAfter = concat(data, concat(data, v.prev->prefix, uSummary), v.next->suffix);
            return routes.cost(uRouteAfter) + routes.cost(vRouteAfter) -
                   routes.route(u.route).cost - routes.route(v.route).cost;
        }

        const SearchNode &first = u.position < v.position ? u : v;
        const SearchNode &second = u.position < v.position ? v : u;
        RouteSummary after = concat(data, first.prev->prefix, visitSummary(data, second.delivery()));
        if (first.next != &second)
        {
            after = concat(data, after, routes.summaryBetween(*first.next, *second.prev));
        }
        after = concat(data, concat(data, after, visitSummary(data, first.delivery())), second.next->suffix);
        return routes.cost(after) - routes.route(u.route).cost;
    }

    bool Swap::conflicts(const SearchNode &u, const SearchNode &v, const SearchRoutes &routes)
    {
        return u.route != v.route &&
               (routes.hasCustomerInRoute(u.customer, v.route, v.id) ||
                routes.hasCustomerInRoute(v.customer, u.route, u.id));
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
                for (int sibling : routes.nodesOf(moved->customer))
                {
                    const SearchNode &other = routes.node(sibling);
                    if (sibling == moved->id)
                    {
                        continue;
                    }
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

    std::optional<double> PartialRelocate::evaluate(
        const SearchNode &u, const SearchNode &target, std::int64_t moved, const SearchRoutes &routes)
    {
        if (u.isDepot() || target.next == nullptr || u.route == target.route || moved < 1 || moved >= u.units ||
            routes.hasCustomerInRoute(u.customer, target.route))
        {
            return std::nullopt;
        }
        const RouteSummary fromAfter = withQuantity(routes, u, u.units - moved);
        const RouteSummary toAfter = insertedAfter(routes, target, {u.customer, moved});
        return routes.cost(fromAfter) + routes.cost(toAfter) -
               routes.route(u.route).cost - routes.route(target.route).cost;
    }

    void PartialRelocate::apply(SearchNode &u, SearchNode &target, std::int64_t moved, SearchRoutes &routes)
    {
        const int fromRoute = u.route;
        routes.setUnits(u, u.units - moved);
        routes.insertAfter({u.customer, moved}, target);
        routes.update(routes.route(target.route));
        routes.update(routes.route(fromRoute));
    }

    bool PartialRelocate::tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes)
    {
        if (routes.freeNodeCount() == 0 || u.units < 2)
        {
            return false;
        }
        SearchNode *bestTarget = nullptr;
        std::int64_t bestMoved = 0;
        double bestDelta = -kImprovementEpsilon;
        const std::int64_t excess = excessUnits(routes.data(), routes.route(u.route).summary(), u.customer);
        for (SearchNode *target : {&v, v.isDepot() ? nullptr : v.prev})
        {
            if (target == nullptr || target->route == u.route)
            {
                continue;
            }
            const std::int64_t room = residualUnits(routes.data(), routes.route(target->route).summary(), u.customer);
            for (std::int64_t moved : candidateAmounts({room, excess}, 1, u.units - 1))
            {
                const std::optional<double> delta = evaluate(u, *target, moved, routes);
                if (!delta)
                {
                    continue;
                }
                ++stats_.evaluated;
                if (*delta < bestDelta)
                {
                    bestDelta = *delta;
                    bestTarget = target;
                    bestMoved = moved;
                }
            }
        }
        if (bestTarget == nullptr)
        {
            return false;
        }
        ++stats_.improving;
        apply(u, *bestTarget, bestMoved, routes);
        ++stats_.applied;
        return true;
    }

    std::optional<double> Transfer::evaluate(
        const SearchNode &u, const SearchNode &sibling, std::int64_t moved, const SearchRoutes &routes)
    {
        if (&u == &sibling || u.isDepot() || u.customer != sibling.customer || u.route == sibling.route ||
            moved < 1 || moved >= u.units)
        {
            return std::nullopt;
        }
        const RouteSummary fromAfter = withQuantity(routes, u, u.units - moved);
        const RouteSummary toAfter = withQuantity(routes, sibling, sibling.units + moved);
        return routes.cost(fromAfter) + routes.cost(toAfter) -
               routes.route(u.route).cost - routes.route(sibling.route).cost;
    }

    void Transfer::apply(SearchNode &u, SearchNode &sibling, std::int64_t moved, SearchRoutes &routes)
    {
        routes.setUnits(u, u.units - moved);
        routes.setUnits(sibling, sibling.units + moved);
        routes.update(routes.route(u.route));
        routes.update(routes.route(sibling.route));
    }

    bool Transfer::tryApply(SearchNode &u, SearchNode &sibling, SearchRoutes &routes)
    {
        const std::int64_t room = residualUnits(routes.data(), routes.route(sibling.route).summary(), u.customer);
        const std::int64_t excess = excessUnits(routes.data(), routes.route(u.route).summary(), u.customer);
        std::int64_t bestMoved = 0;
        double bestDelta = -kImprovementEpsilon;
        for (std::int64_t moved : candidateAmounts({room, excess}, 1, u.units - 1))
        {
            const std::optional<double> delta = evaluate(u, sibling, moved, routes);
            if (!delta)
            {
                continue;
            }
            ++stats_.evaluated;
            if (*delta < bestDelta)
            {
                bestDelta = *delta;
                bestMoved = moved;
            }
        }
        if (bestMoved == 0)
        {
            return false;
        }
        ++stats_.improving;
        apply(u, sibling, bestMoved, routes);
        ++stats_.applied;
        return true;
    }

    std::optional<SplitPlan> SplitInsertion::plan(SearchNode &u, SearchRoutes &routes)
    {
        if (u.isDepot())
        {
            return std::nullopt;
        }
        const ProblemData &data = routes.data();
        const int customer = u.customer;
        const std::int64_t quantity = u.units;

        const auto pieceDelta = [&](SearchNode &target, bool merge, std::int64_t units)
        {
            const RouteSummary after = merge ? withQuantity(routes, target, target.units + units)
                                             : insertedAfter(routes, target, {customer, units});
            return routes.cost(after) - routes.route(target.route).cost;
        };

        struct Candidate
        {
            SearchNode *target = nullptr;
            bool merge = false;
            std::int64_t capacity = 0;
            double ratio = 0.0;
        };
        std::vector<Candidate> candidates;
        const auto consider = [&](SearchNode *target, bool merge)
        {
            if (target == nullptr || target->route == u.route)
            {
                return;
            }
            const std::int64_t room = residualUnits(data, routes.route(target->route).summary(), customer);
            if (room <= 0)
            {
                return;
            }
            const std::int64_t capacity = std::min(room, quantity);
            const double ratio = pieceDelta(*target, merge, capacity) / static_cast<double>(capacity);
            const auto existing = std::find_if(candidates.begin(), candidates.end(), [&](const Candidate &candidate)
                                               { return candidate.target->route == target->route; });
            if (existing == candidates.end())
            {
                candidates.push_back({target, merge, capacity, ratio});
            }
            else if (ratio < existing->ratio)
            {
                *existing = {target, merge, capacity, ratio};
            }
        };

        for (int neighbour : data.neighbours(customer))
        {
            for (int id : routes.nodesOf(neighbour))
            {
                SearchNode &v = routes.node(id);
                if (v.route == u.route || routes.hasCustomerInRoute(customer, v.route))
                {
                    continue;
                }
                consider(&v, false);
                consider(v.prev, false);
            }
        }
        for (int id : routes.nodesOf(customer))
        {
            if (id != u.id)
            {
                consider(&routes.node(id), true);
            }
        }
        if (SearchRoute *empty = routes.firstEmptyRoute())
        {
            consider(&empty->start, false);
        }
        if (candidates.empty())
        {
            return std::nullopt;
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b)
                         { return a.ratio < b.ratio; });

        SplitPlan result;
        std::int64_t remaining = quantity;
        for (const Candidate &candidate : candidates)
        {
            if (remaining == 0 || result.pieces.size() == kMaxSplitRoutes)
            {
                break;
            }
            const std::int64_t take = std::min(candidate.capacity, remaining);
            result.pieces.push_back({candidate.target, candidate.merge, take, pieceDelta(*candidate.target, candidate.merge, take)});
            remaining -= take;
        }
        if (remaining > 0)
        {
            SplitPiece &last = result.pieces.back();
            last.units += remaining;
            last.delta = pieceDelta(*last.target, last.merge, last.units);
        }
        result.delta = routes.cost(withoutNode(routes, u)) - routes.route(u.route).cost;
        for (const SplitPiece &piece : result.pieces)
        {
            result.delta += piece.delta;
        }
        return result;
    }

    void SplitInsertion::apply(SearchNode &u, const SplitPlan &plan, SearchRoutes &routes)
    {
        const int customer = u.customer;
        std::vector<int> touched{u.route};
        routes.remove(u);
        for (const SplitPiece &piece : plan.pieces)
        {
            if (piece.merge)
            {
                routes.setUnits(*piece.target, piece.target->units + piece.units);
            }
            else
            {
                routes.insertAfter({customer, piece.units}, *piece.target);
            }
            touched.push_back(piece.target->route);
        }
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (int index : touched)
        {
            routes.update(routes.route(index));
        }
    }

    bool SplitInsertion::tryApply(SearchNode &u, SearchNode &, SearchRoutes &routes)
    {
        const std::optional<SplitPlan> candidate = plan(u, routes);
        if (!candidate)
        {
            return false;
        }
        ++stats_.evaluated;
        if (candidate->delta >= -kImprovementEpsilon)
        {
            return false;
        }
        ++stats_.improving;
        const auto created = static_cast<std::size_t>(std::count_if(
            candidate->pieces.begin(), candidate->pieces.end(), [](const SplitPiece &piece) { return !piece.merge; }));
        if (routes.freeNodeCount() + 1 < created)
        {
            ++stats_.rejectedByConflict;
            return false;
        }
        apply(u, *candidate, routes);
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
            else if (name == "swap")
            {
                moves.push_back(std::make_unique<Swap>());
            }
            else if (name == "2-opt*")
            {
                moves.push_back(std::make_unique<TwoOptStar>());
            }
            else if (name == "partial-relocate")
            {
                moves.push_back(std::make_unique<PartialRelocate>());
            }
            else if (name == "transfer")
            {
                moves.push_back(std::make_unique<Transfer>());
            }
            else if (name == "split-insertion")
            {
                moves.push_back(std::make_unique<SplitInsertion>());
            }
            else
            {
                throw std::invalid_argument("unknown local search move: " + name);
            }
        }
        return moves;
    }
}
