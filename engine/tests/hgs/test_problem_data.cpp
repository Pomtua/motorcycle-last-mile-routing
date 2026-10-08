#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "router/hgs/problem_data.hpp"
#include "hgs/test_support.hpp"
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

        expect(data.visitCount() == catalog.size(), "visit count must match the catalog");
        expect(catalog.size() > static_cast<std::size_t>(inst.n), "random instance must contain split customers");
        expect(data.visit(0).node == 0 && data.visit(0).twEnd == inst.horizon, "visit 0 must be the depot spanning the horizon");

        bool layoutMatches = true;
        bool matricesMatch = true;
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            const router::Visit &source = catalog[static_cast<std::size_t>(visit) - 1];
            const router::Node &node = inst.nodes[static_cast<std::size_t>(source.nodeIndex)];
            const auto &mapped = data.visit(visit);
            layoutMatches = layoutMatches &&
                            mapped.node == source.nodeIndex &&
                            mapped.weight == source.weight &&
                            mapped.volume == source.volume &&
                            mapped.serviceTime == node.serviceTime &&
                            mapped.twStart == node.twStart &&
                            mapped.twEnd == node.twEnd &&
                            mapped.zoneMask == 0 &&
                            data.catalogVisit(visit).chunkIdx == source.chunkIdx;
            for (int other = 0; other <= static_cast<int>(data.visitCount()); ++other)
            {
                const auto from = static_cast<std::size_t>(mapped.node);
                const auto to = static_cast<std::size_t>(data.visit(other).node);
                matricesMatch = matricesMatch &&
                                data.distance(visit, other) == inst.distanceMatrix[from][to] &&
                                data.duration(visit, other) == inst.durationMatrix[from][to];
            }
        }
        expect(layoutMatches, "visit data must mirror catalog chunks and customer nodes");
        expect(matricesMatch, "visit distances must read the node matrices");
    }

    void testSiblingsAndNeighbours()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(2);
        const std::vector<router::Visit> catalog = router::splitCustomers(inst);
        const std::size_t granularity = 8;
        const router::hgs::ProblemData data(inst, catalog, {{}, 0.0, granularity});

        bool siblingsCorrect = true;
        bool neighboursCorrect = true;
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            const int node = data.visit(visit).node;
            std::vector<int> expectedSiblings;
            for (int other = 1; other <= static_cast<int>(data.visitCount()); ++other)
            {
                if (other != visit && data.visit(other).node == node)
                {
                    expectedSiblings.push_back(other);
                }
            }
            siblingsCorrect = siblingsCorrect && data.siblings(visit) == expectedSiblings;

            const auto &neighbours = data.neighbours(visit);
            const std::size_t candidates = data.visitCount() - 1 - expectedSiblings.size();
            neighboursCorrect = neighboursCorrect && neighbours.size() == std::min(granularity, candidates);
            double previous = -1.0;
            for (int neighbour : neighbours)
            {
                const double proximity = 0.5 * (data.distance(visit, neighbour) + data.distance(neighbour, visit));
                neighboursCorrect = neighboursCorrect &&
                                    data.visit(neighbour).node != node &&
                                    proximity >= previous;
                previous = proximity;
            }
            for (int other = 1; other <= static_cast<int>(data.visitCount()); ++other)
            {
                if (data.visit(other).node == node ||
                    std::find(neighbours.begin(), neighbours.end(), other) != neighbours.end())
                {
                    continue;
                }
                const double proximity = 0.5 * (data.distance(visit, other) + data.distance(other, visit));
                neighboursCorrect = neighboursCorrect && proximity >= previous;
            }
        }
        expect(siblingsCorrect, "siblings must list every other chunk of the same customer");
        expect(neighboursCorrect, "neighbours must be the closest visits of other customers in order");
    }

    void testZones()
    {
        const router::Instance inst = hgs_test::makeRandomInstance(3);
        const std::vector<router::Visit> catalog = router::splitCustomers(inst);
        std::vector<int> zoneOf = hgs_test::makeRandomZones(3, inst, 5);
        const router::hgs::ProblemData data(inst, catalog, {zoneOf, 100.0, 20});

        bool masksCorrect = data.hasZones() && data.zonePenalty() == 100.0;
        for (int visit = 1; visit <= static_cast<int>(data.visitCount()); ++visit)
        {
            const int zone = zoneOf[static_cast<std::size_t>(data.visit(visit).node)];
            masksCorrect = masksCorrect && data.visit(visit).zoneMask == (std::uint64_t{1} << zone);
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
        rejects([](auto &, auto &badCatalog, auto &) { badCatalog[0].nodeIndex = 0; }, "visits at the depot must be rejected");
        rejects([](auto &, auto &badCatalog, auto &) { badCatalog[0].weight = -1.0; }, "negative loads must be rejected");
        rejects([](auto &, auto &, auto &options) { options.granularity = 0; }, "zero granularity must be rejected");

        router::Instance empty;
        empty.horizon = 100.0;
        empty.fleet = {0, 1.0, 1.0};
        empty.nodes.resize(1);
        empty.distanceMatrix = {{0.0}};
        empty.durationMatrix = {{0.0}};
        const router::hgs::ProblemData emptyData(empty, {});
        expect(emptyData.visitCount() == 0, "an instance without customers must have no visits");
    }
}

int main()
{
    testLayout();
    testSiblingsAndNeighbours();
    testZones();
    testValidation();
    return hgs_test::finish("hgs problem_data");
}
