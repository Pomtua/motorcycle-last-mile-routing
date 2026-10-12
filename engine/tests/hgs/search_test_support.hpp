#pragma once

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/delivery.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/route_summary.hpp"
#include "router/hgs/search_routes.hpp"
#include "router/split.hpp"

namespace hgs_test
{
    using RouteLists = std::vector<std::vector<router::hgs::Delivery>>;

    struct Fixture
    {
        router::Instance inst;
        std::vector<router::Visit> catalog;
        std::vector<int> zoneOf;
    };

    inline Fixture makeFixture(std::uint32_t seed, int customers, int zones)
    {
        Fixture fixture;
        fixture.inst = makeRandomInstance(seed, {.customers = customers, .minWindow = 900.0, .maxWindow = 7200.0});
        fixture.catalog = router::splitCustomers(fixture.inst);
        if (zones > 0)
        {
            fixture.zoneOf = makeRandomZones(seed, fixture.inst, zones);
        }
        return fixture;
    }

    inline RouteLists snapshot(const router::hgs::SearchRoutes &routes)
    {
        RouteLists lists(static_cast<std::size_t>(routes.routeCount()));
        for (int index = 0; index < routes.routeCount(); ++index)
        {
            const auto &route = routes.route(index);
            for (const auto *node = route.start.next; node != &route.end; node = node->next)
            {
                lists[static_cast<std::size_t>(index)].push_back(node->delivery());
            }
        }
        return lists;
    }

    inline double totalCost(
        const router::hgs::ProblemData &data,
        const RouteLists &lists,
        const router::hgs::Penalties &penalties)
    {
        double total = 0.0;
        for (const auto &list : lists)
        {
            total += router::hgs::penalizedCost(data, router::hgs::summarizeRoute(data, list), penalties);
        }
        return total;
    }

    inline bool hasCustomerConflict(const RouteLists &lists)
    {
        for (const auto &list : lists)
        {
            std::set<int> customers;
            for (const auto &delivery : list)
            {
                if (!customers.insert(delivery.customer).second)
                {
                    return true;
                }
            }
        }
        return false;
    }

    inline std::vector<const router::hgs::SearchNode *> allTargets(const router::hgs::SearchRoutes &routes)
    {
        std::vector<int> ids = routes.activeNodes();
        std::sort(ids.begin(), ids.end());
        std::vector<const router::hgs::SearchNode *> targets;
        for (int id : ids)
        {
            targets.push_back(&routes.node(id));
        }
        for (int index = 0; index < routes.routeCount(); ++index)
        {
            targets.push_back(&routes.route(index).start);
        }
        return targets;
    }

    inline std::vector<int> sortedActiveNodes(const router::hgs::SearchRoutes &routes)
    {
        std::vector<int> ids = routes.activeNodes();
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    inline bool invariantsHold(const router::hgs::SearchRoutes &routes)
    {
        try
        {
            routes.checkInvariants();
            return true;
        }
        catch (const std::logic_error &)
        {
            return false;
        }
    }

    inline std::size_t indexOf(const router::hgs::SearchNode &node)
    {
        return static_cast<std::size_t>(node.position) - 1;
    }

    inline RouteLists relocated(RouteLists lists, const router::hgs::SearchNode &u, const router::hgs::SearchNode &target)
    {
        auto &source = lists[static_cast<std::size_t>(u.route)];
        const router::hgs::Delivery moved = source[indexOf(u)];
        source.erase(source.begin() + static_cast<std::ptrdiff_t>(indexOf(u)));
        auto &destination = lists[static_cast<std::size_t>(target.route)];
        std::size_t at = target.isDepot() ? 0 : static_cast<std::size_t>(target.position);
        if (target.route == u.route && target.position > u.position)
        {
            --at;
        }
        destination.insert(destination.begin() + static_cast<std::ptrdiff_t>(at), moved);
        return lists;
    }

    inline RouteLists swapped(RouteLists lists, const router::hgs::SearchNode &u, const router::hgs::SearchNode &v)
    {
        std::swap(lists[static_cast<std::size_t>(u.route)][indexOf(u)], lists[static_cast<std::size_t>(v.route)][indexOf(v)]);
        return lists;
    }

    inline RouteLists tailsExchanged(RouteLists lists, const router::hgs::SearchNode &u, const router::hgs::SearchNode &cut)
    {
        auto &uList = lists[static_cast<std::size_t>(u.route)];
        auto &cutList = lists[static_cast<std::size_t>(cut.route)];
        const auto uSplit = uList.begin() + u.position;
        const auto cutSplit = cutList.begin() + cut.position;
        std::vector<router::hgs::Delivery> uHead(uList.begin(), uSplit);
        std::vector<router::hgs::Delivery> uTail(uSplit, uList.end());
        std::vector<router::hgs::Delivery> cutHead(cutList.begin(), cutSplit);
        std::vector<router::hgs::Delivery> cutTail(cutSplit, cutList.end());
        uHead.insert(uHead.end(), cutTail.begin(), cutTail.end());
        cutHead.insert(cutHead.end(), uTail.begin(), uTail.end());
        uList = uHead;
        cutList = cutHead;
        return lists;
    }
}
