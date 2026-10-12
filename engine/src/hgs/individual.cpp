#include "router/hgs/individual.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "router/hgs/route_summary.hpp"

namespace router::hgs
{
    bool isCompleteDeliverySet(const ProblemData &data, const std::vector<Delivery> &deliveries)
    {
        std::vector<std::int64_t> delivered(data.customerCount() + 1, 0);
        for (const Delivery &delivery : deliveries)
        {
            if (delivery.customer < 1 || static_cast<std::size_t>(delivery.customer) > data.customerCount() ||
                delivery.units <= 0)
            {
                return false;
            }
            delivered[static_cast<std::size_t>(delivery.customer)] += delivery.units;
        }
        for (int customer = 1; customer <= static_cast<int>(data.customerCount()); ++customer)
        {
            if (delivered[static_cast<std::size_t>(customer)] != data.customer(customer).demandUnits)
            {
                return false;
            }
        }
        return true;
    }

    SplitSummary summarizeSplits(const ProblemData &data, const Individual &individual)
    {
        SplitSummary summary;
        summary.deliveries = individual.giantTour.size();
        summary.canonicalChunks = data.tokenCount();
        summary.pieceFractionHistogram.assign(std::size(kSplitHistogramEdges), 0);

        std::vector<std::vector<std::int64_t>> pieces(data.customerCount() + 1);
        for (const Delivery &delivery : individual.giantTour)
        {
            pieces[static_cast<std::size_t>(delivery.customer)].push_back(delivery.units);
        }
        for (int customer = 1; customer <= static_cast<int>(data.customerCount()); ++customer)
        {
            auto &served = pieces[static_cast<std::size_t>(customer)];
            if (served.empty())
            {
                continue;
            }
            ++summary.customers;
            summary.maxDeliveriesPerCustomer = std::max(summary.maxDeliveriesPerCustomer, served.size());

            std::vector<std::int64_t> canonical;
            for (int token : data.tokensOf(customer))
            {
                canonical.push_back(data.token(token).units);
            }
            for (std::int64_t units : served)
            {
                const auto match = std::find(canonical.begin(), canonical.end(), units);
                if (match != canonical.end())
                {
                    ++summary.deliveriesMatchingCanonical;
                    canonical.erase(match);
                }
            }

            if (served.size() < 2)
            {
                continue;
            }
            ++summary.splitCustomers;
            const double demand = static_cast<double>(data.customer(customer).demandUnits);
            for (std::int64_t units : served)
            {
                const double fraction = static_cast<double>(units) / demand;
                summary.smallestPieceFraction = std::min(summary.smallestPieceFraction, fraction);
                std::size_t bucket = 0;
                while (bucket + 1 < std::size(kSplitHistogramEdges) && fraction >= kSplitHistogramEdges[bucket])
                {
                    ++bucket;
                }
                ++summary.pieceFractionHistogram[bucket];
            }
        }
        return summary;
    }

    Individual makeIndividual(const ProblemData &data, std::vector<std::vector<Delivery>> routes)
    {
        routes.erase(
            std::remove_if(routes.begin(), routes.end(), [](const auto &route) { return route.empty(); }),
            routes.end());
        if (routes.size() > static_cast<std::size_t>(data.fleetSize()))
        {
            throw std::invalid_argument("individual uses more routes than the fleet");
        }

        Individual individual;
        for (const auto &route : routes)
        {
            individual.giantTour.insert(individual.giantTour.end(), route.begin(), route.end());
        }
        if (!isCompleteDeliverySet(data, individual.giantTour))
        {
            throw std::invalid_argument("individual deliveries must be positive and add up to every customer's demand");
        }

        std::vector<std::size_t> lastRouteOf(data.customerCount() + 1, routes.size());
        for (std::size_t routeIndex = 0; routeIndex < routes.size(); ++routeIndex)
        {
            for (const Delivery &delivery : routes[routeIndex])
            {
                std::size_t &last = lastRouteOf[static_cast<std::size_t>(delivery.customer)];
                if (last == routeIndex)
                {
                    throw std::invalid_argument("a route cannot visit the same customer twice");
                }
                last = routeIndex;
            }
        }

        for (const auto &route : routes)
        {
            int previous = ProblemData::kDepot;
            for (const Delivery &delivery : route)
            {
                individual.arcs.emplace_back(previous, delivery.customer);
                previous = delivery.customer;
            }
            individual.arcs.emplace_back(previous, ProblemData::kDepot);
            individual.cost += routeCost(data, summarizeRoute(data, route));
        }
        std::sort(individual.arcs.begin(), individual.arcs.end());
        individual.routes = std::move(routes);
        return individual;
    }

    Individual individualFromSolution(const ProblemData &data, const Solution &solution)
    {
        std::vector<std::vector<Delivery>> routes;
        routes.reserve(solution.routes.size());
        for (const Route &route : solution.routes)
        {
            std::vector<Delivery> deliveries;
            deliveries.reserve(route.stops.size());
            for (const Visit &stop : route.stops)
            {
                if (stop.nodeIndex < 1 || static_cast<std::size_t>(stop.nodeIndex) > data.customerCount())
                {
                    throw std::invalid_argument("solution visits a node that is not a customer");
                }
                deliveries.push_back({stop.nodeIndex, toUnits(stop.weight)});
            }
            routes.push_back(std::move(deliveries));
        }
        return makeIndividual(data, std::move(routes));
    }

    Solution toSolution(const ProblemData &data, const Individual &individual)
    {
        std::vector<int> deliveryCount(data.customerCount() + 1, 0);
        for (const Delivery &delivery : individual.giantTour)
        {
            ++deliveryCount[static_cast<std::size_t>(delivery.customer)];
        }

        std::vector<int> nextIndex(data.customerCount() + 1, 0);
        Solution solution;
        solution.routes.reserve(individual.routes.size());
        for (const auto &route : individual.routes)
        {
            Route exported;
            exported.stops.reserve(route.size());
            for (const Delivery &delivery : route)
            {
                const auto customer = static_cast<std::size_t>(delivery.customer);
                exported.stops.push_back({
                    delivery.customer,
                    nextIndex[customer]++,
                    deliveryCount[customer],
                    toWeight(delivery.units),
                    data.volumeOf(delivery)});
            }
            solution.routes.push_back(std::move(exported));
        }
        return solution;
    }
}
