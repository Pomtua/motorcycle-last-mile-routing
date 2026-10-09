#pragma once

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/route_summary.hpp"
#include "router/hgs/search_routes.hpp"
#include "router/split.hpp"

namespace hgs_test
{
    using RouteLists = std::vector<std::vector<int>>;

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
                lists[static_cast<std::size_t>(index)].push_back(node->visit);
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

    inline bool hasCustomerConflict(const router::hgs::ProblemData &data, const RouteLists &lists)
    {
        for (const auto &list : lists)
        {
            std::set<int> nodes;
            for (int visit : list)
            {
                if (!nodes.insert(data.visit(visit).node).second)
                {
                    return true;
                }
            }
        }
        return false;
    }

    inline std::vector<const router::hgs::SearchNode *> allTargets(const router::hgs::SearchRoutes &routes)
    {
        std::vector<const router::hgs::SearchNode *> targets;
        for (int visit = 1; visit <= static_cast<int>(routes.data().visitCount()); ++visit)
        {
            targets.push_back(&routes.node(visit));
        }
        for (int index = 0; index < routes.routeCount(); ++index)
        {
            targets.push_back(&routes.route(index).start);
        }
        return targets;
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

    inline std::vector<int>::iterator positionAfter(std::vector<int> &list, const router::hgs::SearchNode &target)
    {
        return target.isDepot() ? list.begin() : std::find(list.begin(), list.end(), target.visit) + 1;
    }

    inline RouteLists relocated(RouteLists lists, int visit, const router::hgs::SearchNode &target)
    {
        for (auto &list : lists)
        {
            list.erase(std::remove(list.begin(), list.end(), visit), list.end());
        }
        auto &destination = lists[static_cast<std::size_t>(target.route)];
        destination.insert(positionAfter(destination, target), visit);
        return lists;
    }

    inline RouteLists swapped(RouteLists lists, int first, int second)
    {
        for (auto &list : lists)
        {
            for (int &visit : list)
            {
                visit = visit == first ? second : visit == second ? first : visit;
            }
        }
        return lists;
    }

    inline RouteLists tailsExchanged(RouteLists lists, const router::hgs::SearchNode &u, const router::hgs::SearchNode &cut)
    {
        auto &uList = lists[static_cast<std::size_t>(u.route)];
        auto &cutList = lists[static_cast<std::size_t>(cut.route)];
        const auto uSplit = positionAfter(uList, u);
        const auto cutSplit = positionAfter(cutList, cut);
        std::vector<int> uHead(uList.begin(), uSplit);
        std::vector<int> uTail(uSplit, uList.end());
        std::vector<int> cutHead(cutList.begin(), cutSplit);
        std::vector<int> cutTail(cutSplit, cutList.end());
        uHead.insert(uHead.end(), cutTail.begin(), cutTail.end());
        cutHead.insert(cutHead.end(), uTail.begin(), uTail.end());
        uList = uHead;
        cutList = cutHead;
        return lists;
    }
}
