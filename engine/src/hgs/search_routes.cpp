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
          nodes_(data.tokenCount() + 2 * static_cast<std::size_t>(data.fleetSize()) + 1),
          nodesOfCustomer_(data.customerCount() + 1),
          routes_(static_cast<std::size_t>(data.fleetSize()))
    {
        for (int id = 0; id < static_cast<int>(nodes_.size()); ++id)
        {
            node(id).id = id;
        }
        for (int index = 0; index < routeCount(); ++index)
        {
            SearchRoute &current = route(index);
            current.index = index;
            current.start.id = -1;
            current.end.id = -1;
            current.start.route = index;
            current.end.route = index;
            current.start.next = &current.end;
            current.end.prev = &current.start;
        }
        releaseAll();
    }

    void SearchRoutes::releaseAll()
    {
        freeNodes_.clear();
        for (int id = static_cast<int>(nodes_.size()) - 1; id >= 1; --id)
        {
            node(id).customer = ProblemData::kDepot;
            node(id).units = 0;
            freeNodes_.push_back(id);
        }
        active_.clear();
        for (auto &list : nodesOfCustomer_)
        {
            list.clear();
        }
    }

    int SearchRoutes::allocate(const Delivery &delivery)
    {
        if (freeNodes_.empty())
        {
            throw std::length_error("search routes node pool is exhausted");
        }
        const int id = freeNodes_.back();
        freeNodes_.pop_back();
        SearchNode &created = node(id);
        created.customer = delivery.customer;
        created.units = delivery.units;
        active_.push_back(id);
        nodesOfCustomer_[static_cast<std::size_t>(delivery.customer)].push_back(id);
        return id;
    }

    void SearchRoutes::load(const Individual &individual, const Penalties &penalties)
    {
        validatePenalties(penalties);
        if (individual.routes.size() > routes_.size())
        {
            throw std::invalid_argument("individual uses more routes than the fleet");
        }
        std::vector<Delivery> all;
        for (const auto &deliveries : individual.routes)
        {
            std::vector<int> customers;
            for (const Delivery &delivery : deliveries)
            {
                customers.push_back(delivery.customer);
            }
            std::sort(customers.begin(), customers.end());
            if (std::adjacent_find(customers.begin(), customers.end()) != customers.end())
            {
                throw std::invalid_argument("a loaded route cannot visit the same customer twice");
            }
            all.insert(all.end(), deliveries.begin(), deliveries.end());
        }
        if (!isCompleteDeliverySet(data_, all))
        {
            throw std::invalid_argument("loaded routes must deliver every customer's demand exactly");
        }
        if (all.size() > nodeCapacity())
        {
            throw std::invalid_argument("loaded routes need more deliveries than the node pool holds");
        }

        penalties_ = penalties;
        releaseAll();
        for (SearchRoute &current : routes_)
        {
            current.start.next = &current.end;
            current.end.prev = &current.start;
        }
        for (std::size_t index = 0; index < individual.routes.size(); ++index)
        {
            SearchRoute &current = routes_[index];
            SearchNode *previous = &current.start;
            for (const Delivery &delivery : individual.routes[index])
            {
                SearchNode &linked = node(allocate(delivery));
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

    std::vector<std::vector<Delivery>> SearchRoutes::exportRoutes() const
    {
        std::vector<std::vector<Delivery>> exported;
        for (const SearchRoute &current : routes_)
        {
            if (current.empty())
            {
                continue;
            }
            std::vector<Delivery> deliveries;
            deliveries.reserve(static_cast<std::size_t>(current.size));
            for (const SearchNode *linked = current.start.next; linked != &current.end; linked = linked->next)
            {
                deliveries.push_back(linked->delivery());
            }
            exported.push_back(std::move(deliveries));
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
        RouteSummary summary = visitSummary(data_, first.delivery());
        for (const SearchNode *linked = &first; linked != &last;)
        {
            linked = linked->next;
            summary = concat(data_, summary, visitSummary(data_, linked->delivery()));
        }
        return summary;
    }

    bool SearchRoutes::hasCustomerInRoute(int customer, int routeIndex, int ignoredNode) const
    {
        for (int id : nodesOf(customer))
        {
            if (id != ignoredNode && node(id).route == routeIndex)
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

    void SearchRoutes::swapNodes(SearchNode &first, SearchNode &second)
    {
        if (first.next == &second)
        {
            moveAfter(first, second);
            return;
        }
        if (second.next == &first)
        {
            moveAfter(second, first);
            return;
        }
        SearchNode &beforeFirst = *first.prev;
        SearchNode &beforeSecond = *second.prev;
        moveAfter(first, beforeSecond);
        moveAfter(second, beforeFirst);
    }

    void SearchRoutes::exchangeTails(SearchNode &first, SearchNode &second)
    {
        SearchRoute &firstRoute = route(first.route);
        SearchRoute &secondRoute = route(second.route);
        std::vector<SearchNode *> firstTail;
        for (SearchNode *linked = first.next; linked != &firstRoute.end; linked = linked->next)
        {
            firstTail.push_back(linked);
        }
        std::vector<SearchNode *> secondTail;
        for (SearchNode *linked = second.next; linked != &secondRoute.end; linked = linked->next)
        {
            secondTail.push_back(linked);
        }

        const auto relink = [](SearchNode &head, const std::vector<SearchNode *> &tail, SearchNode &end)
        {
            SearchNode *previous = &head;
            for (SearchNode *linked : tail)
            {
                linked->route = head.route;
                linked->prev = previous;
                previous->next = linked;
                previous = linked;
            }
            previous->next = &end;
            end.prev = previous;
        };
        relink(first, secondTail, firstRoute.end);
        relink(second, firstTail, secondRoute.end);
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
            prefix = concat(data_, prefix, visitSummary(data_, linked->delivery()));
            linked->prefix = prefix;
        }
        current.end.position = position + 1;
        current.end.prefix = concat(data_, prefix, depotSummary(data_));

        RouteSummary suffix = depotSummary(data_);
        current.end.suffix = suffix;
        for (SearchNode *linked = current.end.prev; linked != &current.start; linked = linked->prev)
        {
            suffix = concat(data_, visitSummary(data_, linked->delivery()), suffix);
            linked->suffix = suffix;
        }
        current.start.suffix = concat(data_, depotSummary(data_), suffix);

        current.size = position;
        current.cost = cost(current.end.prefix);
        current.lastModified = ++modifications_;
    }

    void SearchRoutes::checkInvariants() const
    {
        std::vector<int> routeOfNode(nodes_.size(), -1);
        std::vector<std::int64_t> deliveredUnits(data_.customerCount() + 1, 0);
        for (const SearchRoute &current : routes_)
        {
            std::vector<Delivery> deliveries;
            std::vector<int> customers;
            const SearchNode *previous = &current.start;
            for (const SearchNode *linked = current.start.next; linked != &current.end; linked = linked->next)
            {
                if (linked == nullptr || linked->prev != previous || linked->route != current.index ||
                    linked->position != static_cast<int>(deliveries.size()) + 1)
                {
                    fail("broken links or positions in route " + std::to_string(current.index));
                }
                if (linked->id <= 0 || routeOfNode[static_cast<std::size_t>(linked->id)] != -1)
                {
                    fail("node " + std::to_string(linked->id) + " is invalid or linked twice");
                }
                if (linked->units <= 0)
                {
                    fail("node " + std::to_string(linked->id) + " delivers a non-positive quantity");
                }
                routeOfNode[static_cast<std::size_t>(linked->id)] = current.index;
                deliveredUnits[static_cast<std::size_t>(linked->customer)] += linked->units;
                deliveries.push_back(linked->delivery());
                customers.push_back(linked->customer);
                previous = linked;
            }
            if (current.end.prev != previous || current.size != static_cast<int>(deliveries.size()))
            {
                fail("route " + std::to_string(current.index) + " size or tail is stale");
            }
            std::sort(customers.begin(), customers.end());
            if (std::adjacent_find(customers.begin(), customers.end()) != customers.end())
            {
                fail("route " + std::to_string(current.index) + " visits a customer twice");
            }

            const RouteSummary fresh = summarizeRoute(data_, deliveries);
            if (!sameSummary(current.end.prefix, fresh) || !sameSummary(current.start.suffix, fresh) ||
                !sameValue(current.cost, cost(fresh)))
            {
                fail("route " + std::to_string(current.index) + " summary is stale");
            }
            RouteSummary expectedPrefix = depotSummary(data_);
            for (std::size_t cut = 0; cut < deliveries.size(); ++cut)
            {
                expectedPrefix = concat(data_, expectedPrefix, visitSummary(data_, deliveries[cut]));
                RouteSummary expectedSuffix = depotSummary(data_);
                for (std::size_t tail = deliveries.size(); tail > cut; --tail)
                {
                    expectedSuffix = concat(data_, visitSummary(data_, deliveries[tail - 1]), expectedSuffix);
                }
                const SearchNode *linked = current.start.next;
                for (std::size_t step = 0; step < cut; ++step)
                {
                    linked = linked->next;
                }
                if (!sameSummary(linked->prefix, expectedPrefix) || !sameSummary(linked->suffix, expectedSuffix))
                {
                    fail("prefix or suffix of node " + std::to_string(linked->id) + " is stale");
                }
            }
        }

        for (int id : active_)
        {
            if (routeOfNode[static_cast<std::size_t>(id)] == -1)
            {
                fail("active node " + std::to_string(id) + " is not routed");
            }
        }
        std::size_t indexed = 0;
        for (int customer = 1; customer <= static_cast<int>(data_.customerCount()); ++customer)
        {
            if (deliveredUnits[static_cast<std::size_t>(customer)] != data_.customer(customer).demandUnits)
            {
                fail("customer " + std::to_string(customer) + " does not receive its exact demand");
            }
            for (int id : nodesOf(customer))
            {
                if (node(id).customer != customer || routeOfNode[static_cast<std::size_t>(id)] == -1)
                {
                    fail("customer index of node " + std::to_string(id) + " is stale");
                }
                ++indexed;
            }
        }
        if (indexed != active_.size() || active_.size() + freeNodes_.size() != nodeCapacity())
        {
            fail("node pool bookkeeping is inconsistent");
        }
    }
}
