#pragma once

#include <cstdint>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/route_summary.hpp"

namespace router::hgs
{
    struct SearchNode
    {
        int visit = ProblemData::kDepot;
        int route = 0;
        int position = 0;
        SearchNode *prev = nullptr;
        SearchNode *next = nullptr;
        RouteSummary prefix;
        RouteSummary suffix;

        bool isDepot() const { return visit == ProblemData::kDepot; }
    };

    struct SearchRoute
    {
        int index = 0;
        SearchNode start;
        SearchNode end;
        int size = 0;
        double cost = 0.0;
        std::uint64_t lastModified = 0;

        bool empty() const { return size == 0; }
        const RouteSummary &summary() const { return end.prefix; }
    };

    class SearchRoutes
    {
    public:
        explicit SearchRoutes(const ProblemData &data);
        SearchRoutes(const SearchRoutes &) = delete;
        SearchRoutes &operator=(const SearchRoutes &) = delete;

        void load(const Individual &individual, const Penalties &penalties);
        std::vector<std::vector<int>> exportRoutes() const;

        const ProblemData &data() const { return data_; }
        const Penalties &penalties() const { return penalties_; }
        SearchNode &node(int visit) { return nodes_[static_cast<std::size_t>(visit)]; }
        const SearchNode &node(int visit) const { return nodes_[static_cast<std::size_t>(visit)]; }
        SearchRoute &route(int index) { return routes_[static_cast<std::size_t>(index)]; }
        const SearchRoute &route(int index) const { return routes_[static_cast<std::size_t>(index)]; }
        int routeCount() const { return static_cast<int>(routes_.size()); }
        SearchRoute *firstEmptyRoute();
        std::uint64_t modificationCount() const { return modifications_; }

        double cost(const RouteSummary &summary) const { return penalizedCost(data_, summary, penalties_); }
        double totalCost() const;
        RouteSummary summaryBetween(const SearchNode &first, const SearchNode &last) const;
        bool hasSiblingInRoute(int visit, int route) const;

        void moveAfter(SearchNode &node, SearchNode &after);
        void update(SearchRoute &route);
        void checkInvariants() const;

    private:
        const ProblemData &data_;
        Penalties penalties_;
        std::vector<SearchNode> nodes_;
        std::vector<SearchRoute> routes_;
        std::uint64_t modifications_ = 0;
    };
}
