#pragma once

#include <cstdint>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/delivery.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/route_summary.hpp"

namespace router::hgs
{
    struct SearchNode
    {
        int id = 0;
        int customer = ProblemData::kDepot;
        std::int64_t units = 0;
        int route = 0;
        int position = 0;
        SearchNode *prev = nullptr;
        SearchNode *next = nullptr;
        RouteSummary prefix;
        RouteSummary suffix;

        bool isDepot() const { return customer == ProblemData::kDepot; }
        Delivery delivery() const { return {customer, units}; }
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
        std::vector<std::vector<Delivery>> exportRoutes() const;

        const ProblemData &data() const { return data_; }
        const Penalties &penalties() const { return penalties_; }
        SearchNode &node(int id) { return nodes_[static_cast<std::size_t>(id)]; }
        const SearchNode &node(int id) const { return nodes_[static_cast<std::size_t>(id)]; }
        const std::vector<int> &activeNodes() const { return active_; }
        const std::vector<int> &nodesOf(int customer) const { return nodesOfCustomer_[static_cast<std::size_t>(customer)]; }
        std::size_t nodeCapacity() const { return nodes_.size() - 1; }
        std::size_t freeNodeCount() const { return freeNodes_.size(); }
        std::uint64_t structureVersion() const { return structureVersion_; }
        SearchRoute &route(int index) { return routes_[static_cast<std::size_t>(index)]; }
        const SearchRoute &route(int index) const { return routes_[static_cast<std::size_t>(index)]; }
        int routeCount() const { return static_cast<int>(routes_.size()); }
        SearchRoute *firstEmptyRoute();
        std::uint64_t modificationCount() const { return modifications_; }

        double cost(const RouteSummary &summary) const { return penalizedCost(data_, summary, penalties_); }
        double totalCost() const;
        RouteSummary summaryBetween(const SearchNode &first, const SearchNode &last) const;
        bool hasCustomerInRoute(int customer, int route, int ignoredNode = 0) const;

        void moveAfter(SearchNode &node, SearchNode &after);
        void swapNodes(SearchNode &first, SearchNode &second);
        void exchangeTails(SearchNode &first, SearchNode &second);
        SearchNode &insertAfter(const Delivery &delivery, SearchNode &after);
        void remove(SearchNode &removed);
        void setUnits(SearchNode &changed, std::int64_t units);
        void update(SearchRoute &route);
        void checkInvariants() const;

    private:
        int allocate(const Delivery &delivery);
        void releaseAll();

        const ProblemData &data_;
        Penalties penalties_;
        std::vector<SearchNode> nodes_;
        std::vector<int> freeNodes_;
        std::vector<int> active_;
        std::vector<std::size_t> activeIndex_;
        std::vector<std::vector<int>> nodesOfCustomer_;
        std::vector<SearchRoute> routes_;
        std::uint64_t modifications_ = 0;
        std::uint64_t structureVersion_ = 0;
    };
}
