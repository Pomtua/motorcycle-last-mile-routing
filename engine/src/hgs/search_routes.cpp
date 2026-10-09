#include "router/hgs/search_routes.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace router::hgs
{
    namespace
    {
        bool sameValue(double a, double b)
        {
            return std::abs(a - b) <= 1e-9 * std::max({1.0, std::abs(a), std::abs(b)});
        }

        bool sameSummary(const RouteSummary &a, const RouteSummary &b)
        {
            return a.first == b.first && a.last == b.last && a.visits == b.visits && a.zones == b.zones &&
                   sameValue(a.distance, b.distance) && sameValue(a.duration, b.duration) &&
                   sameValue(a.timeWarp, b.timeWarp) && sameValue(a.earliest, b.earliest) &&
                   sameValue(a.latest, b.latest) && sameValue(a.weight, b.weight) &&
                   sameValue(a.volume, b.volume);
        }

        void fail(const std::string &message)
        {
            throw std::logic_error("search routes invariant broken: " + message);
        }
    }

    SearchRoutes::SearchRoutes(const ProblemData &data)
        : data_(data),
          nodes_(data.visitCount() + 1),
          routes_(static_cast<std::size_t>(data.fleetSize()))
    {
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            node(visit).visit = visit;
        }
        for (int index = 0; index < routeCount(); ++index)
        {
            SearchRoute &current = route(index);
            current.index = index;
            current.start.route = index;
            current.end.route = index;
            current.start.next = &current.end;
            current.end.prev = &current.start;
        }
    }

    void SearchRoutes::load(const Individual &individual, const Penalties &penalties)
    {
        validatePenalties(penalties);
        if (individual.routes.size() > routes_.size())
        {
            throw std::invalid_argument("individual uses more routes than the fleet");
        }
        std::vector<bool> seen(nodes_.size(), false);
        std::size_t served = 0;
        for (const auto &visits : individual.routes)
        {
            for (int visit : visits)
            {
                if (visit < 1 || static_cast<std::size_t>(visit) >= nodes_.size() ||
                    seen[static_cast<std::size_t>(visit)])
                {
                    throw std::invalid_argument("individual routes must be a permutation of all visits");
                }
                seen[static_cast<std::size_t>(visit)] = true;
                ++served;
            }
        }
        if (served != data_.visitCount())
        {
            throw std::invalid_argument("individual routes must serve every visit");
        }

        penalties_ = penalties;
        for (SearchRoute &current : routes_)
        {
            current.start.next = &current.end;
            current.end.prev = &current.start;
        }
        for (std::size_t index = 0; index < individual.routes.size(); ++index)
        {
            SearchRoute &current = routes_[index];
            SearchNode *previous = &current.start;
            for (int visit : individual.routes[index])
            {
                SearchNode &linked = node(visit);
                linked.prev = previous;
                previous->next = &linked;
                previous = &linked;
            }
            previous->next = &current.end;
            current.end.prev = previous;
        }
        for (SearchRoute &current : routes_)
        {
            update(current);
        }
    }

    std::vector<std::vector<int>> SearchRoutes::exportRoutes() const
    {
        std::vector<std::vector<int>> exported;
        for (const SearchRoute &current : routes_)
        {
            if (current.empty())
            {
                continue;
            }
            std::vector<int> visits;
            visits.reserve(static_cast<std::size_t>(current.size));
            for (const SearchNode *linked = current.start.next; linked != &current.end; linked = linked->next)
            {
                visits.push_back(linked->visit);
            }
            exported.push_back(std::move(visits));
        }
        return exported;
    }

    SearchRoute *SearchRoutes::firstEmptyRoute()
    {
        for (SearchRoute &current : routes_)
        {
            if (current.empty())
            {
                return &current;
            }
        }
        return nullptr;
    }

    double SearchRoutes::totalCost() const
    {
        double total = 0.0;
        for (const SearchRoute &current : routes_)
        {
            total += current.cost;
        }
        return total;
    }

    RouteSummary SearchRoutes::summaryBetween(const SearchNode &first, const SearchNode &last) const
    {
        RouteSummary summary = visitSummary(data_, first.visit);
        for (const SearchNode *linked = &first; linked != &last;)
        {
            linked = linked->next;
            summary = concat(data_, summary, visitSummary(data_, linked->visit));
        }
        return summary;
    }

    bool SearchRoutes::hasSiblingInRoute(int visit, int routeIndex) const
    {
        for (int sibling : data_.siblings(visit))
        {
            if (node(sibling).route == routeIndex)
            {
                return true;
            }
        }
        return false;
    }

    void SearchRoutes::moveAfter(SearchNode &moved, SearchNode &after)
    {
        moved.prev->next = moved.next;
        moved.next->prev = moved.prev;
        moved.prev = &after;
        moved.next = after.next;
        after.next->prev = &moved;
        after.next = &moved;
        moved.route = after.route;
    }

    void SearchRoutes::update(SearchRoute &current)
    {
        RouteSummary prefix = depotSummary(data_);
        current.start.prefix = prefix;
        current.start.position = 0;
        int position = 0;
        for (SearchNode *linked = current.start.next; linked != &current.end; linked = linked->next)
        {
            linked->route = current.index;
            linked->position = ++position;
            prefix = concat(data_, prefix, visitSummary(data_, linked->visit));
            linked->prefix = prefix;
        }
        current.end.position = position + 1;
        current.end.prefix = concat(data_, prefix, depotSummary(data_));

        RouteSummary suffix = depotSummary(data_);
        current.end.suffix = suffix;
        for (SearchNode *linked = current.end.prev; linked != &current.start; linked = linked->prev)
        {
            suffix = concat(data_, visitSummary(data_, linked->visit), suffix);
            linked->suffix = suffix;
        }
        current.start.suffix = concat(data_, depotSummary(data_), suffix);

        current.size = position;
        current.cost = cost(current.end.prefix);
        current.lastModified = ++modifications_;
    }

    void SearchRoutes::checkInvariants() const
    {
        std::vector<int> routeOfVisit(nodes_.size(), -1);
        for (const SearchRoute &current : routes_)
        {
            std::vector<int> visits;
            const SearchNode *previous = &current.start;
            for (const SearchNode *linked = current.start.next; linked != &current.end; linked = linked->next)
            {
                if (linked == nullptr || linked->prev != previous || linked->route != current.index ||
                    linked->position != static_cast<int>(visits.size()) + 1)
                {
                    fail("broken links or positions in route " + std::to_string(current.index));
                }
                if (routeOfVisit[static_cast<std::size_t>(linked->visit)] != -1)
                {
                    fail("visit " + std::to_string(linked->visit) + " appears twice");
                }
                routeOfVisit[static_cast<std::size_t>(linked->visit)] = current.index;
                visits.push_back(linked->visit);
                previous = linked;
            }
            if (current.end.prev != previous || current.size != static_cast<int>(visits.size()))
            {
                fail("route " + std::to_string(current.index) + " size or tail is stale");
            }

            const RouteSummary fresh = summarizeRoute(data_, visits);
            if (!sameSummary(current.end.prefix, fresh) || !sameSummary(current.start.suffix, fresh) ||
                !sameValue(current.cost, cost(fresh)))
            {
                fail("route " + std::to_string(current.index) + " summary is stale");
            }
            for (std::size_t cut = 0; cut < visits.size(); ++cut)
            {
                const SearchNode &linked = node(visits[cut]);
                const std::vector<int> head(visits.begin(), visits.begin() + static_cast<std::ptrdiff_t>(cut) + 1);
                const std::vector<int> tail(visits.begin() + static_cast<std::ptrdiff_t>(cut), visits.end());
                RouteSummary expectedPrefix = depotSummary(data_);
                for (int visit : head)
                {
                    expectedPrefix = concat(data_, expectedPrefix, visitSummary(data_, visit));
                }
                RouteSummary expectedSuffix = depotSummary(data_);
                for (auto visit = tail.rbegin(); visit != tail.rend(); ++visit)
                {
                    expectedSuffix = concat(data_, visitSummary(data_, *visit), expectedSuffix);
                }
                if (!sameSummary(linked.prefix, expectedPrefix) || !sameSummary(linked.suffix, expectedSuffix))
                {
                    fail("prefix or suffix of visit " + std::to_string(visits[cut]) + " is stale");
                }
            }
        }
        for (int visit = 1; visit <= static_cast<int>(data_.visitCount()); ++visit)
        {
            if (routeOfVisit[static_cast<std::size_t>(visit)] == -1)
            {
                fail("visit " + std::to_string(visit) + " is not routed");
            }
            for (int sibling : data_.siblings(visit))
            {
                if (routeOfVisit[static_cast<std::size_t>(sibling)] == routeOfVisit[static_cast<std::size_t>(visit)])
                {
                    fail("visit " + std::to_string(visit) + " shares a route with a sibling chunk");
                }
            }
        }
    }
}
