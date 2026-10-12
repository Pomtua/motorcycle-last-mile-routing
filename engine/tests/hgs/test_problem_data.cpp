#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/split.hpp"

using hgs_test::expect;
using hgs_test::expectInvalidArgument;

namespace
{
    void testLayout()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(1);
        const std::vector<router::Visit> catalog = router::splitCustomers(inst);
        const router::hgs::ProblemData data(inst, catalog);

        expect(data.customerCount() == static_cast<std::size_t>(inst.n), "customer count must match n");
        expect(data.tokenCount() == catalog.size() && catalog.size() > static_cast<std::size_t>(inst.n),
               "tokens must mirror the canonical chunks, including split customers");
        expect(data.customer(0).node == 0 && data.customer(0).twEnd == inst.horizon,
               "customer 0 must be the depot spanning the horizon");

        bool customersMatch = true;
        for (int customer = 1; customer <= inst.n; ++customer)
        {
            const router::Node &node = inst.nodes[static_cast<std::size_t>(customer)];
            const auto &mapped = data.customer(customer);
            customersMatch = customersMatch &&
                             mapped.node == customer &&
                             mapped.demandUnits == router::hgs::toUnits(node.demandWeight) &&
                             mapped.demandVolume == node.demandVolume &&
                             hgs_test::near(mapped.volumePerUnit * static_cast<double>(mapped.demandUnits), node.demandVolume) &&
                             mapped.serviceTime == node.serviceTime &&
                             mapped.twStart == node.twStart &&
                             mapped.twEnd == node.twEnd &&
                             mapped.zoneMask == 0;
        }
        expect(customersMatch, "customer data must mirror the instance nodes");

        bool tokensMatch = true;
        std::vector<std::vector<int>> expectedTokens(data.customerCount() + 1);
        for (int token = 1; token <= static_cast<int>(data.tokenCount()); ++token)
        {
            const router::Visit &source = catalog[static_cast<std::size_t>(token) - 1];
            tokensMatch = tokensMatch &&
                          data.token(token).customer == source.nodeIndex &&
                          data.token(token).units == router::hgs::toUnits(source.weight);
            expectedTokens[static_cast<std::size_t>(source.nodeIndex)].push_back(token);
        }
        for (int customer = 1; customer <= inst.n; ++customer)
        {
            tokensMatch = tokensMatch && data.tokensOf(customer) == expectedTokens[static_cast<std::size_t>(customer)];
        }
        expect(tokensMatch, "tokens must keep catalog order, quantities and per-customer grouping");

        bool matricesMatch = true;
        for (int from = 0; from <= inst.n; ++from)
        {
            for (int to = 0; to <= inst.n; ++to)
            {
                matricesMatch = matricesMatch &&
                                data.distance(from, to) == inst.distanceMatrix[static_cast<std::size_t>(from)][static_cast<std::size_t>(to)] &&
                                data.duration(from, to) == inst.durationMatrix[static_cast<std::size_t>(from)][static_cast<std::size_t>(to)];
            }
        }
        expect(matricesMatch, "customer distances must read the node matrices");
    }

    void testNeighbours()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(2);
        const std::size_t granularity = 8;
        const router::hgs::ProblemData data(inst, router::splitCustomers(inst), {{}, 0.0, granularity});

        bool neighboursCorrect = true;
        for (int customer = 1; customer <= inst.n; ++customer)
        {
            const auto &neighbours = data.neighbours(customer);
            neighboursCorrect = neighboursCorrect &&
                                neighbours.size() == std::min(granularity, data.customerCount() - 1);
            double previous = -1.0;
            for (int neighbour : neighbours)
            {
                const double proximity = 0.5 * (data.distance(customer, neighbour) + data.distance(neighbour, customer));
                neighboursCorrect = neighboursCorrect && neighbour != customer && proximity >= previous;
                previous = proximity;
            }
            for (int other = 1; other <= inst.n; ++other)
            {
                if (other == customer || std::find(neighbours.begin(), neighbours.end(), other) != neighbours.end())
                {
                    continue;
                }
                const double proximity = 0.5 * (data.distance(customer, other) + data.distance(other, customer));
                neighboursCorrect = neighboursCorrect && proximity >= previous;
            }
        }
        expect(neighboursCorrect, "neighbours must be the closest other customers in order");
    }

    void testZones()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(3);
        const std::vector<router::Visit> catalog = router::splitCustomers(inst);
        std::vector<int> zoneOf = hgs_test::makeRandomZones(3, inst, 5);
        const router::hgs::ProblemData data(inst, catalog, {zoneOf, 100.0, 20});

        bool masksCorrect = data.hasZones() && data.zonePenalty() == 100.0;
        for (int customer = 1; customer <= inst.n; ++customer)
        {
            masksCorrect = masksCorrect &&
                           data.customer(customer).zoneMask == (std::uint64_t{1} << zoneOf[static_cast<std::size_t>(customer)]);
        }
        expect(masksCorrect, "zone masks must encode each customer zone as one bit");

        zoneOf[1] = router::hgs::ProblemData::kMaxZones - 1;
        expect(router::hgs::ProblemData(inst, catalog, {zoneOf, 1.0, 20}).hasZones(),
               "the highest zone index must still fit the mask");

        zoneOf[1] = router::hgs::ProblemData::kMaxZones;
        expectInvalidArgument([&]() { router::hgs::ProblemData(inst, catalog, {zoneOf, 1.0, 20}); },
                              "zones beyond the mask width must be rejected");
        zoneOf[1] = -1;
        expectInvalidArgument([&]() { router::hgs::ProblemData(inst, catalog, {zoneOf, 1.0, 20}); },
                              "customers without a zone must be rejected");
        zoneOf.pop_back();
        expectInvalidArgument([&]() { router::hgs::ProblemData(inst, catalog, {zoneOf, 1.0, 20}); },
                              "zone assignments must cover every node");
        expectInvalidArgument([&]() { router::hgs::ProblemData(inst, catalog, {{}, 1.0, 20}); },
                              "zone penalties without zones must be rejected");
    }

    void testValidation()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(4, {.customers = 5});
        const std::vector<router::Visit> catalog = router::splitCustomers(inst);
        const auto rejects = [&](auto mutate, const char *message)
        {
            router::Instance bad = inst;
            std::vector<router::Visit> badCatalog = catalog;
            router::hgs::ProblemOptions options;
            mutate(bad, badCatalog, options);
            expectInvalidArgument([&]() { router::hgs::ProblemData(bad, badCatalog, options); }, message);
        };

        rejects([](auto &bad, auto &, auto &) { bad.distanceMatrix[1][2] = -1.0; }, "negative distances must be rejected");
        rejects([](auto &bad, auto &, auto &) { bad.durationMatrix[2][1] = std::numeric_limits<double>::quiet_NaN(); }, "NaN durations must be rejected");
        rejects([](auto &bad, auto &, auto &) { bad.distanceMatrix.pop_back(); }, "non-square matrices must be rejected");
        rejects([](auto &bad, auto &, auto &) { bad.fleet.weightCapacity = 0.0; }, "zero capacity must be rejected");
        rejects([](auto &bad, auto &, auto &) { bad.fleet.size = -1; }, "negative fleets must be rejected");
        rejects([](auto &bad, auto &, auto &) { bad.nodes[1].twStart = bad.nodes[1].twEnd + 1; }, "inverted time windows must be rejected");
        rejects([](auto &bad, auto &, auto &) { bad.nodes.pop_back(); }, "node count must match n");
        rejects([](auto &bad, auto &, auto &) { bad.nodes[2].demandWeight = -1.0; }, "negative demand must be rejected");
        rejects([](auto &, auto &badCatalog, auto &) { badCatalog[0].nodeIndex = 0; }, "chunks at the depot must be rejected");
        rejects([](auto &, auto &badCatalog, auto &) { badCatalog[0].weight = 0.0; }, "zero-weight chunks must be rejected");
        rejects([](auto &, auto &badCatalog, auto &) { badCatalog[0].weight += 0.01; }, "chunks that overshoot a customer's demand must be rejected");
        rejects([](auto &, auto &badCatalog, auto &) { badCatalog.pop_back(); }, "chunks that leave demand uncovered must be rejected");
        rejects([](auto &, auto &, auto &options) { options.granularity = 0; }, "zero granularity must be rejected");

        router::Instance empty;
        empty.horizon = 100.0;
        empty.fleet = {0, 1.0, 1.0};
        empty.nodes.resize(1);
        empty.distanceMatrix = {{0.0}};
        empty.durationMatrix = {{0.0}};
        const router::hgs::ProblemData emptyData(empty, {});
        expect(emptyData.customerCount() == 0 && emptyData.tokenCount() == 0,
               "an instance without customers must have no customers or tokens");
    }
}

int main()
{
    testLayout();
    testNeighbours();
    testZones();
    testValidation();
    return hgs_test::finish("hgs problem_data");
}
