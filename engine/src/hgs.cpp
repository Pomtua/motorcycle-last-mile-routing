#include "router/hgs.hpp"
#include "router/validate.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace router
{
    namespace
    {
        constexpr double kWeightTolerance = 1e-6;
        constexpr double kVolumeTolerance = 1e-9;
        constexpr double kTimeTolerance = 1e-6;

        bool hgsDeadlineExpired(const HgsDeadline &deadline)
        {
            return deadline && std::chrono::steady_clock::now() >= *deadline;
        }

        void checkHgsDeadline(const HgsDeadline &deadline)
        {
            if (hgsDeadlineExpired(deadline))
            {
                throw HgsTimeLimitReached("HGS time limit reached");
            }
        }

        HgsRouteSegmentEvaluation evaluateGiantTourSegmentImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const std::vector<HgsGene> &giantTour,
            std::size_t beginIndex,
            std::size_t endIndex,
            const std::vector<int> *zoneOf,
            const HgsDeadline &deadline = std::nullopt);

        void validateCrossoverParents(
            const std::vector<HgsGene> &firstParent,
            const std::vector<HgsGene> &secondParent)
        {
            const std::size_t count = firstParent.size();
            if (!isCompleteGiantTourPermutation(firstParent, count) ||
                !isCompleteGiantTourPermutation(secondParent, count))
            {
                throw std::invalid_argument(
                    "OX parents must be complete permutations of the same visits");
            }
        }

        std::vector<double> normalizedAverageRanks(
            const std::vector<double> &values,
            bool higherIsBetter)
        {
            std::vector<std::size_t> order(values.size());
            for (std::size_t index = 0; index < order.size(); ++index)
            {
                order[index] = index;
            }
            std::sort(
                order.begin(), order.end(),
                [&](std::size_t first, std::size_t second)
                {
                    return higherIsBetter
                               ? values[first] > values[second]
                               : values[first] < values[second];
                });

            std::vector<double> ranks(values.size());
            for (std::size_t begin = 0; begin < order.size();)
            {
                std::size_t end = begin + 1;
                while (end < order.size() &&
                       values[order[end]] == values[order[begin]])
                {
                    ++end;
                }

                const double rank =
                    (static_cast<double>(begin) + 1.0 +
                     static_cast<double>(end)) /
                    (2.0 * static_cast<double>(values.size()));
                for (std::size_t position = begin; position < end; ++position)
                {
                    ranks[order[position]] = rank;
                }
                begin = end;
            }
            return ranks;
        }

        std::size_t trimHgsSubpopulation(
            const std::vector<Visit> &visitCatalog,
            std::vector<HgsIndividual> &individuals,
            std::size_t targetSize,
            std::size_t nClose,
            std::size_t nElite,
            const HgsDeadline &deadline)
        {
            const std::size_t originalSize = individuals.size();
            while (individuals.size() > targetSize)
            {
                checkHgsDeadline(deadline);
                const std::vector<HgsFitness> fitness =
                    computeHgsFitness(
                        visitCatalog, individuals, nClose, nElite, deadline);
                std::vector<bool> isClone(individuals.size(), false);
                bool hasClone = false;
                for (std::size_t first = 0; first < individuals.size(); ++first)
                {
                    checkHgsDeadline(deadline);
                    for (std::size_t second = first + 1;
                         second < individuals.size(); ++second)
                    {
                        checkHgsDeadline(deadline);
                        if (individuals[first].evaluation.penalizedCost ==
                                individuals[second].evaluation.penalizedCost ||
                            directedBrokenPairsDistance(
                                visitCatalog,
                                individuals[first].decodedSolution,
                                individuals[second].decodedSolution, deadline) == 0.0)
                        {
                            isClone[first] = true;
                            isClone[second] = true;
                            hasClone = true;
                        }
                    }
                }

                std::size_t worst = individuals.size();
                for (std::size_t index = 0; index < individuals.size(); ++index)
                {
                    checkHgsDeadline(deadline);
                    if (hasClone && !isClone[index])
                    {
                        continue;
                    }
                    if (worst == individuals.size() ||
                        fitness[index].biasedFitness >
                            fitness[worst].biasedFitness ||
                        (fitness[index].biasedFitness ==
                             fitness[worst].biasedFitness &&
                         individuals[index].evaluation.penalizedCost >=
                             individuals[worst].evaluation.penalizedCost))
                    {
                        worst = index;
                    }
                }

                individuals.erase(
                    individuals.begin() +
                    static_cast<std::ptrdiff_t>(worst));
            }
            return originalSize - individuals.size();
        }

        struct HgsRouteAdjacency
        {
            std::vector<std::size_t> predecessor;
            std::vector<std::size_t> successor;
        };

        HgsRouteAdjacency buildRouteAdjacency(
            const std::vector<Visit> &visitCatalog,
            const Solution &solution,
            const HgsDeadline &deadline)
        {
            checkHgsDeadline(deadline);
            const std::vector<HgsGene> giantTour =
                encodeSolutionAsGiantTour(solution, visitCatalog);
            const std::size_t depot = visitCatalog.size();
            HgsRouteAdjacency adjacency{
                std::vector<std::size_t>(visitCatalog.size(), depot),
                std::vector<std::size_t>(visitCatalog.size(), depot)};

            std::size_t beginIndex = 0;
            for (const Route &route : solution.routes)
            {
                checkHgsDeadline(deadline);
                if (route.stops.empty())
                {
                    throw std::invalid_argument(
                        "broken-pairs distance requires non-empty routes");
                }

                const std::size_t endIndex =
                    beginIndex + route.stops.size();
                for (std::size_t position = beginIndex;
                     position < endIndex;
                     ++position)
                {
                    checkHgsDeadline(deadline);
                    const std::size_t visitIndex =
                        giantTour[position].visitIndex;
                    adjacency.predecessor[visitIndex] =
                        position == beginIndex
                            ? depot
                            : giantTour[position - 1].visitIndex;
                    adjacency.successor[visitIndex] =
                        position + 1 == endIndex
                            ? depot
                            : giantTour[position + 1].visitIndex;
                }
                beginIndex = endIndex;
            }

            return adjacency;
        }

        struct HgsSplitArc
        {
            std::size_t endIndex = 0;
            double penalizedCost = 0.0;
        };

        void validatePenaltyWeights(
            const HgsPenaltyWeights &penaltyWeights,
            double zonePenalty)
        {
            for (double value : {
                     penaltyWeights.weightPenalty,
                     penaltyWeights.volumePenalty,
                     penaltyWeights.timeWarpPenalty,
                     zonePenalty})
            {
                if (!std::isfinite(value) || value < 0.0)
                {
                    throw std::invalid_argument(
                        "HGS penalty weights must be finite and non-negative");
                }
            }
        }

        double computePenalizedCost(
            double distanceCost,
            int routeZoneExcess,
            const HgsConstraintViolations &violations,
            const HgsPenaltyWeights &penaltyWeights,
            double zonePenalty)
        {
            return distanceCost +
                   zonePenalty *
                       static_cast<double>(
                           routeZoneExcess) +
                   penaltyWeights.weightPenalty *
                       violations.weightExcess +
                   penaltyWeights.volumePenalty *
                       violations.volumeExcess +
                   penaltyWeights.timeWarpPenalty *
                       violations.timeWarp;
        }

        std::vector<std::vector<HgsSplitArc>>
        buildSplitArcs(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const std::vector<HgsGene> &giantTour,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty,
            const HgsDeadline &deadline)
        {
            std::vector<std::vector<HgsSplitArc>>
                arcsByStart(giantTour.size());

            for (std::size_t beginIndex = 0;
                 beginIndex < giantTour.size();
                 ++beginIndex)
            {
                checkHgsDeadline(deadline);
                for (std::size_t endIndex =
                         beginIndex + 1;
                     endIndex <= giantTour.size();
                     ++endIndex)
                {
                    checkHgsDeadline(deadline);
                    const HgsRouteSegmentEvaluation
                        routeEvaluation =
                            evaluateGiantTourSegmentImpl(
                                inst, visitCatalog, giantTour,
                                beginIndex, endIndex, zoneOf, deadline);

                    if (routeEvaluation.hasCustomerConflict)
                    {
                        break;
                    }

                    if (routeEvaluation
                                .violations.weightExcess >
                            inst.fleet.weightCapacity +
                                kWeightTolerance ||
                        routeEvaluation
                                .violations.volumeExcess >
                            inst.fleet.volumeCapacity +
                                kVolumeTolerance)
                    {
                        break;
                    }

                    arcsByStart[beginIndex].push_back(
                        {endIndex,
                         computePenalizedCost(
                             routeEvaluation.distanceCost,
                             routeEvaluation.routeZoneExcess,
                             routeEvaluation.violations,
                             penaltyWeights,
                             zonePenalty)});
                }
            }

            return arcsByStart;
        }

        HgsIndividual decodeGiantTourImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const std::vector<HgsGene> &giantTour,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty,
            const HgsDeadline &deadline = std::nullopt)
        {
            checkHgsDeadline(deadline);
            validatePenaltyWeights(
                penaltyWeights, zonePenalty);

            if (!isCompleteGiantTourPermutation(
                    giantTour, visitCatalog.size()))
            {
                throw std::invalid_argument(
                    "cannot decode an incomplete HGS "
                    "giant-tour permutation");
            }

            HgsIndividual individual;
            individual.giantTour = giantTour;

            if (giantTour.empty())
            {
                individual.evaluation =
                    aggregateHgsIndividualEvaluation(
                        {},
                        penaltyWeights,
                        zonePenalty);
                return individual;
            }

            if (inst.fleet.size <= 0)
            {
                throw std::invalid_argument(
                    "HGS decoding requires a positive fleet size");
            }

            const std::size_t visitCount =
                giantTour.size();
            const std::size_t maxRoutes =
                std::min(
                    visitCount,
                    static_cast<std::size_t>(
                        inst.fleet.size));
            const double infinity =
                std::numeric_limits<double>::infinity();
            const std::size_t noParent =
                visitCount + 1;

            const std::vector<std::vector<HgsSplitArc>>
                arcsByStart =
                    buildSplitArcs(
                        inst,
                        visitCatalog,
                        giantTour,
                        penaltyWeights,
                        zoneOf,
                        zonePenalty,
                        deadline);

            std::vector<std::vector<std::size_t>> parent(
                maxRoutes + 1,
                std::vector<std::size_t>(
                    visitCount + 1,
                    noParent));
            std::vector<double> previous(
                visitCount + 1,
                infinity);
            std::vector<double> current(
                visitCount + 1,
                infinity);

            previous[0] = 0.0;

            double bestCost = infinity;
            std::size_t bestRouteCount = 0;

            for (std::size_t routeCount = 1;
                 routeCount <= maxRoutes;
                 ++routeCount)
            {
                checkHgsDeadline(deadline);
                std::fill(
                    current.begin(),
                    current.end(),
                    infinity);

                for (std::size_t beginIndex = 0;
                     beginIndex < visitCount;
                     ++beginIndex)
                {
                    checkHgsDeadline(deadline);
                    if (!std::isfinite(
                            previous[beginIndex]))
                    {
                        continue;
                    }

                    for (const HgsSplitArc &arc :
                         arcsByStart[beginIndex])
                    {
                        checkHgsDeadline(deadline);
                        const double candidateCost =
                            previous[beginIndex] +
                            arc.penalizedCost;

                        if (candidateCost <
                            current[arc.endIndex])
                        {
                            current[arc.endIndex] =
                                candidateCost;
                            parent[routeCount]
                                  [arc.endIndex] =
                                beginIndex;
                        }
                    }
                }

                if (current[visitCount] < bestCost)
                {
                    bestCost = current[visitCount];
                    bestRouteCount = routeCount;
                }

                previous.swap(current);
            }

            if (bestRouteCount == 0)
            {
                throw HgsSplitFailure(
                    "HGS Split found no hard-valid "
                    "partition within fleet size");
            }

            std::vector<std::pair<
                std::size_t,
                std::size_t>>
                segments;
            std::size_t endIndex = visitCount;

            for (std::size_t routeCount =
                     bestRouteCount;
                 routeCount > 0;
                 --routeCount)
            {
                checkHgsDeadline(deadline);
                const std::size_t beginIndex =
                    parent[routeCount][endIndex];

                if (beginIndex == noParent)
                {
                    throw std::logic_error(
                        "HGS Split parent chain is incomplete");
                }

                segments.push_back(
                    {beginIndex, endIndex});
                endIndex = beginIndex;
            }

            if (endIndex != 0)
            {
                throw std::logic_error(
                    "HGS Split parent chain does not "
                    "reach the giant-tour start");
            }

            std::reverse(
                segments.begin(),
                segments.end());

            std::vector<HgsRouteSegmentEvaluation>
                routeEvaluations;
            routeEvaluations.reserve(segments.size());
            individual.decodedSolution.routes.reserve(
                segments.size());

            for (const auto [begin, end] : segments)
            {
                checkHgsDeadline(deadline);
                HgsRouteSegmentEvaluation routeEvaluation =
                    evaluateGiantTourSegmentImpl(
                        inst, visitCatalog, giantTour,
                        begin, end, zoneOf, deadline);

                individual.decodedSolution.routes.push_back(
                    routeEvaluation.route);
                routeEvaluations.push_back(
                    std::move(routeEvaluation));
            }

            individual.evaluation =
                aggregateHgsIndividualEvaluation(
                    routeEvaluations,
                    penaltyWeights,
                    zonePenalty);

            return individual;
        }

        HgsIndividual makeHgsIndividualFromSolutionImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const Solution &solution,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty)
        {
            validatePenaltyWeights(penaltyWeights, zonePenalty);

            if (inst.fleet.size < 0 ||
                solution.routes.size() >
                    static_cast<std::size_t>(inst.fleet.size))
            {
                throw std::invalid_argument(
                    "HGS solution exceeds the available fleet");
            }

            HgsIndividual individual;
            individual.giantTour =
                encodeSolutionAsGiantTour(solution, visitCatalog);
            individual.decodedSolution.routes.reserve(
                solution.routes.size());

            std::vector<HgsRouteSegmentEvaluation> routeEvaluations;
            routeEvaluations.reserve(solution.routes.size());

            std::size_t beginIndex = 0;
            for (const Route &route : solution.routes)
            {
                const std::size_t endIndex =
                    beginIndex + route.stops.size();
                HgsRouteSegmentEvaluation routeEvaluation =
                    zoneOf == nullptr
                        ? evaluateGiantTourSegment(
                              inst, visitCatalog, individual.giantTour,
                              beginIndex, endIndex)
                        : evaluateGiantTourSegment(
                              inst, visitCatalog, individual.giantTour,
                              beginIndex, endIndex, *zoneOf);

                individual.decodedSolution.routes.push_back(
                    routeEvaluation.route);
                routeEvaluations.push_back(
                    std::move(routeEvaluation));
                beginIndex = endIndex;
            }

            individual.evaluation =
                aggregateHgsIndividualEvaluation(
                    routeEvaluations, penaltyWeights, zonePenalty);
            return individual;
        }

        double excessAbove(
            double value,
            double limit,
            double tolerance)
        {
            if (value <= limit + tolerance)
            {
                return 0.0;
            }

            return value - limit;
        }

        HgsRouteSegmentEvaluation evaluateGiantTourSegmentImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const std::vector<HgsGene> &giantTour,
            std::size_t beginIndex,
            std::size_t endIndex,
            const std::vector<int> *zoneOf,
            const HgsDeadline &deadline)
        {
            checkHgsDeadline(deadline);
            if (beginIndex >= endIndex ||
                endIndex > giantTour.size())
            {
                throw std::invalid_argument(
                    "HGS route segment must be a non-empty "
                    "half-open range inside the giant tour");
            }

            if (zoneOf != nullptr &&
                zoneOf->size() != inst.nodes.size())
            {
                throw std::invalid_argument(
                    "zone assignment size must match "
                    "instance node count");
            }

            HgsRouteSegmentEvaluation evaluation;
            evaluation.route.stops.reserve(
                endIndex - beginIndex);

            std::set<int> customers;
            std::set<int> usedZones;

            double totalWeight = 0.0;
            double totalVolume = 0.0;
            double clock = 0.0;
            int currentNode = 0;

            for (std::size_t position = beginIndex;
                 position < endIndex;
                 ++position)
            {
                checkHgsDeadline(deadline);
                const HgsGene gene = giantTour[position];

                if (gene.visitIndex >= visitCatalog.size())
                {
                    throw std::invalid_argument(
                        "HGS gene visit index is outside "
                        "the visit catalog");
                }

                const Visit &visit =
                    visitCatalog[gene.visitIndex];

                if (visit.nodeIndex <= 0 ||
                    visit.nodeIndex >=
                        static_cast<int>(inst.nodes.size()))
                {
                    throw std::invalid_argument(
                        "HGS visit contains an invalid "
                        "customer node index");
                }

                evaluation.route.stops.push_back(visit);

                if (!customers.insert(
                        visit.nodeIndex).second)
                {
                    evaluation.hasCustomerConflict = true;
                }

                if (zoneOf != nullptr)
                {
                    const int zone =
                        (*zoneOf)[static_cast<std::size_t>(
                            visit.nodeIndex)];

                    if (zone < 0)
                    {
                        throw std::invalid_argument(
                            "customer must have a "
                            "non-negative zone assignment");
                    }

                    usedZones.insert(zone);
                }

                totalWeight += visit.weight;
                totalVolume += visit.volume;

                evaluation.distanceCost +=
                    inst.distanceMatrix[
                        static_cast<std::size_t>(
                            currentNode)]
                        [static_cast<std::size_t>(
                            visit.nodeIndex)];

                double arrival =
                    clock +
                    inst.durationMatrix[
                        static_cast<std::size_t>(
                            currentNode)]
                        [static_cast<std::size_t>(
                            visit.nodeIndex)];

                const Node &node =
                    inst.nodes[static_cast<std::size_t>(
                        visit.nodeIndex)];

                if (arrival < node.twStart)
                {
                    arrival = node.twStart;
                }
                else if (arrival > node.twEnd)
                {
                    if (arrival >
                        node.twEnd + kTimeTolerance)
                    {
                        evaluation.violations.timeWarp +=
                            arrival - node.twEnd;
                    }

                    arrival = node.twEnd;
                }

                clock = arrival + node.serviceTime;
                currentNode = visit.nodeIndex;
            }

            evaluation.distanceCost +=
                inst.distanceMatrix[
                    static_cast<std::size_t>(currentNode)][0];

            const double finishTime =
                clock +
                inst.durationMatrix[
                    static_cast<std::size_t>(currentNode)][0];

            if (finishTime >
                inst.horizon + kTimeTolerance)
            {
                evaluation.violations.timeWarp +=
                    finishTime - inst.horizon;
            }

            evaluation.violations.weightExcess =
                excessAbove(
                    totalWeight,
                    inst.fleet.weightCapacity,
                    kWeightTolerance);
            evaluation.violations.volumeExcess =
                excessAbove(
                    totalVolume,
                    inst.fleet.volumeCapacity,
                    kVolumeTolerance);

            if (!usedZones.empty())
            {
                evaluation.routeZoneExcess =
                    static_cast<int>(
                        usedZones.size()) -
                    1;
            }

            return evaluation;
        }

        HgsIndividual educateHgsRelocateImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const HgsIndividual &individual,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty,
            std::size_t maxAcceptedMoves,
            const HgsDeadline &deadline = std::nullopt)
        {
            HgsIndividual current = makeHgsIndividualFromSolutionImpl(
                inst, visitCatalog, individual.decodedSolution,
                penaltyWeights, zoneOf, zonePenalty);
            const auto evaluateRoute = [&](const std::vector<HgsGene> &genes)
            {
                return genes.empty()
                           ? HgsRouteSegmentEvaluation{}
                           : evaluateGiantTourSegmentImpl(
                                 inst, visitCatalog, genes, 0, genes.size(), zoneOf);
            };
            const auto routeScore = [&](const HgsRouteSegmentEvaluation &route)
            {
                return computePenalizedCost(
                    route.distanceCost, route.routeZoneExcess,
                    route.violations, penaltyWeights, zonePenalty);
            };

            for (std::size_t accepted = 0; accepted < maxAcceptedMoves; ++accepted)
            {
                if (hgsDeadlineExpired(deadline)) { return current; }
                const std::size_t routeCount = current.decodedSolution.routes.size();
                std::vector<std::vector<HgsGene>> routeGenes;
                std::vector<double> routeCosts;
                std::size_t offset = 0;
                for (const Route &route : current.decodedSolution.routes)
                {
                    if (hgsDeadlineExpired(deadline)) { return current; }
                    const std::size_t end = offset + route.stops.size();
                    routeGenes.emplace_back(
                        current.giantTour.begin() + static_cast<std::ptrdiff_t>(offset),
                        current.giantTour.begin() + static_cast<std::ptrdiff_t>(end));
                    routeCosts.push_back(routeScore(evaluateRoute(routeGenes.back())));
                    offset = end;
                }
                const std::size_t targetCount =
                    routeCount + (routeCount < static_cast<std::size_t>(inst.fleet.size));
                bool improved = false;
                for (std::size_t source = 0; source < routeCount && !improved; ++source)
                {
                    if (hgsDeadlineExpired(deadline)) { return current; }
                    for (std::size_t stop = 0;
                         stop < routeGenes[source].size() && !improved; ++stop)
                    {
                        if (hgsDeadlineExpired(deadline)) { return current; }
                        auto reduced = routeGenes[source];
                        const HgsGene moved = reduced[stop];
                        reduced.erase(reduced.begin() + static_cast<std::ptrdiff_t>(stop));
                        const auto sourceEvaluation = evaluateRoute(reduced);
                        for (std::size_t target = 0; target < targetCount && !improved; ++target)
                        {
                            if (hgsDeadlineExpired(deadline)) { return current; }
                            const std::vector<HgsGene> targetBase =
                                target == source ? reduced :
                                target < routeCount ? routeGenes[target] :
                                                      std::vector<HgsGene>{};
                            for (std::size_t position = 0; position <= targetBase.size(); ++position)
                            {
                                if (hgsDeadlineExpired(deadline)) { return current; }
                                if (source == target && position == stop)
                                {
                                    continue;
                                }
                                auto inserted = targetBase;
                                inserted.insert(
                                    inserted.begin() + static_cast<std::ptrdiff_t>(position), moved);
                                const auto targetEvaluation = evaluateRoute(inserted);
                                if (targetEvaluation.hasCustomerConflict)
                                {
                                    continue;
                                }
                                const double oldCost = routeCosts[source] +
                                    (target != source && target < routeCount ? routeCosts[target] : 0.0);
                                const double newCost = routeScore(targetEvaluation) +
                                    (target != source ? routeScore(sourceEvaluation) : 0.0);
                                if (newCost >= oldCost - 1e-6)
                                {
                                    continue;
                                }

                                Solution candidate = current.decodedSolution;
                                if (target != source)
                                {
                                    candidate.routes[source] = sourceEvaluation.route;
                                }
                                if (target == routeCount)
                                {
                                    candidate.routes.push_back(targetEvaluation.route);
                                }
                                else
                                {
                                    candidate.routes[target] = targetEvaluation.route;
                                }
                                candidate.routes.erase(
                                    std::remove_if(
                                        candidate.routes.begin(), candidate.routes.end(),
                                        [](const Route &route) { return route.stops.empty(); }),
                                    candidate.routes.end());
                                current = makeHgsIndividualFromSolutionImpl(
                                    inst, visitCatalog, candidate,
                                    penaltyWeights, zoneOf, zonePenalty);
                                improved = true;
                                break;
                            }
                        }
                    }
                }
                if (!improved)
                {
                    break;
                }
            }
            return current;
        }

        bool improveHgsAdditionalMoves(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            HgsIndividual &current,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty,
            const HgsDeadline &deadline)
        {
            const auto evaluateRoute = [&](const std::vector<HgsGene> &genes)
            {
                return genes.empty()
                           ? HgsRouteSegmentEvaluation{}
                           : evaluateGiantTourSegmentImpl(
                                 inst, visitCatalog, genes, 0, genes.size(), zoneOf);
            };
            const auto score = [&](const HgsRouteSegmentEvaluation &route)
            {
                return computePenalizedCost(
                    route.distanceCost, route.routeZoneExcess,
                    route.violations, penaltyWeights, zonePenalty);
            };
            std::vector<std::vector<HgsGene>> genes;
            std::vector<double> costs;
            std::size_t offset = 0;
            for (const Route &route : current.decodedSolution.routes)
            {
                if (hgsDeadlineExpired(deadline)) { return false; }
                const std::size_t end = offset + route.stops.size();
                genes.emplace_back(
                    current.giantTour.begin() + static_cast<std::ptrdiff_t>(offset),
                    current.giantTour.begin() + static_cast<std::ptrdiff_t>(end));
                costs.push_back(score(evaluateRoute(genes.back())));
                offset = end;
            }
            const auto tryMove = [&](
                std::size_t first, const std::vector<HgsGene> &firstGenes,
                std::size_t second, const std::vector<HgsGene> &secondGenes)
            {
                const auto firstRoute = evaluateRoute(firstGenes);
                const auto secondRoute = first == second
                    ? HgsRouteSegmentEvaluation{} : evaluateRoute(secondGenes);
                if (firstRoute.hasCustomerConflict || secondRoute.hasCustomerConflict)
                {
                    return false;
                }
                const double oldCost = costs[first] + (first != second ? costs[second] : 0.0);
                const double newCost = score(firstRoute) + (first != second ? score(secondRoute) : 0.0);
                if (newCost >= oldCost - 1e-6)
                {
                    return false;
                }
                Solution candidate = current.decodedSolution;
                candidate.routes[first] = firstRoute.route;
                if (first != second)
                {
                    candidate.routes[second] = secondRoute.route;
                }
                candidate.routes.erase(
                    std::remove_if(
                        candidate.routes.begin(), candidate.routes.end(),
                        [](const Route &route) { return route.stops.empty(); }),
                    candidate.routes.end());
                current = makeHgsIndividualFromSolutionImpl(
                    inst, visitCatalog, candidate, penaltyWeights, zoneOf, zonePenalty);
                return true;
            };

            for (std::size_t first = 0; first < genes.size(); ++first)
            {
                if (hgsDeadlineExpired(deadline)) { return false; }
                for (std::size_t second = first; second < genes.size(); ++second)
                {
                    if (hgsDeadlineExpired(deadline)) { return false; }
                    for (std::size_t a = 0; a < genes[first].size(); ++a)
                    {
                        if (hgsDeadlineExpired(deadline)) { return false; }
                        const std::size_t start = first == second ? a + 1 : 0;
                        for (std::size_t b = start; b < genes[second].size(); ++b)
                        {
                            if (hgsDeadlineExpired(deadline)) { return false; }
                            auto firstGenes = genes[first];
                            auto secondGenes = genes[second];
                            if (first == second)
                            {
                                std::swap(firstGenes[a], firstGenes[b]);
                            }
                            else
                            {
                                std::swap(firstGenes[a], secondGenes[b]);
                            }
                            if (tryMove(first, firstGenes, second, secondGenes))
                            {
                                return true;
                            }
                        }
                    }
                }
            }

            for (std::size_t route = 0; route < genes.size(); ++route)
            {
                if (hgsDeadlineExpired(deadline)) { return false; }
                for (std::size_t begin = 0; begin < genes[route].size(); ++begin)
                {
                    if (hgsDeadlineExpired(deadline)) { return false; }
                    for (std::size_t end = begin + 2; end <= genes[route].size(); ++end)
                    {
                        if (hgsDeadlineExpired(deadline)) { return false; }
                        auto reversed = genes[route];
                        std::reverse(
                            reversed.begin() + static_cast<std::ptrdiff_t>(begin),
                            reversed.begin() + static_cast<std::ptrdiff_t>(end));
                        if (tryMove(route, reversed, route, reversed))
                        {
                            return true;
                        }
                    }
                }
            }

            for (std::size_t first = 0; first < genes.size(); ++first)
            {
                if (hgsDeadlineExpired(deadline)) { return false; }
                for (std::size_t second = first + 1; second < genes.size(); ++second)
                {
                    if (hgsDeadlineExpired(deadline)) { return false; }
                    for (std::size_t a = 0; a <= genes[first].size(); ++a)
                    {
                        if (hgsDeadlineExpired(deadline)) { return false; }
                        for (std::size_t b = 0; b <= genes[second].size(); ++b)
                        {
                            if (hgsDeadlineExpired(deadline)) { return false; }
                            std::vector<HgsGene> firstGenes(
                                genes[first].begin(),
                                genes[first].begin() + static_cast<std::ptrdiff_t>(a));
                            firstGenes.insert(
                                firstGenes.end(),
                                genes[second].begin() + static_cast<std::ptrdiff_t>(b),
                                genes[second].end());
                            std::vector<HgsGene> secondGenes(
                                genes[second].begin(),
                                genes[second].begin() + static_cast<std::ptrdiff_t>(b));
                            secondGenes.insert(
                                secondGenes.end(),
                                genes[first].begin() + static_cast<std::ptrdiff_t>(a),
                                genes[first].end());
                            if (tryMove(first, firstGenes, second, secondGenes))
                            {
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }

        HgsIndividual educateHgsIndividualImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const HgsIndividual &individual,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty,
            std::size_t maxAcceptedMoves,
            const HgsDeadline &deadline = std::nullopt)
        {
            HgsIndividual current = makeHgsIndividualFromSolutionImpl(
                inst, visitCatalog, individual.decodedSolution,
                penaltyWeights, zoneOf, zonePenalty);
            for (std::size_t accepted = 0; accepted < maxAcceptedMoves; ++accepted)
            {
                if (hgsDeadlineExpired(deadline)) { return current; }
                HgsIndividual relocated = educateHgsRelocateImpl(
                    inst, visitCatalog, current, penaltyWeights, zoneOf, zonePenalty, 1, deadline);
                if (relocated.evaluation.penalizedCost < current.evaluation.penalizedCost - 1e-6)
                {
                    current = std::move(relocated);
                    continue;
                }
                if (hgsDeadlineExpired(deadline)) { return current; }
                if (!improveHgsAdditionalMoves(
                        inst, visitCatalog, current, penaltyWeights, zoneOf, zonePenalty, deadline))
                {
                    break;
                }
            }
            return current;
        }

        HgsIndividual repairHgsIndividualImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const HgsIndividual &individual,
            const HgsPenaltyWeights &penaltyWeights,
            const std::vector<int> *zoneOf,
            double zonePenalty,
            std::size_t maxAcceptedMovesPerAttempt,
            const HgsDeadline &deadline = std::nullopt)
        {
            HgsIndividual current = makeHgsIndividualFromSolutionImpl(
                inst, visitCatalog, individual.decodedSolution,
                penaltyWeights, zoneOf, zonePenalty);
            HgsIndividual best = current;
            if (current.evaluation.feasible)
            {
                return current;
            }
            for (double factor : {10.0, 100.0})
            {
                if (hgsDeadlineExpired(deadline)) { break; }
                const auto scale = [&](double value)
                {
                    if (value > std::numeric_limits<double>::max() / factor)
                    {
                        throw std::overflow_error("HGS repair penalty overflow");
                    }
                    return value * factor;
                };
                const HgsPenaltyWeights repairWeights = {
                    scale(penaltyWeights.weightPenalty),
                    scale(penaltyWeights.volumePenalty),
                    scale(penaltyWeights.timeWarpPenalty)};
                current = educateHgsIndividualImpl(
                    inst, visitCatalog, current, repairWeights,
                    zoneOf, zonePenalty, maxAcceptedMovesPerAttempt, deadline);
                HgsIndividual rescored = makeHgsIndividualFromSolutionImpl(
                    inst, visitCatalog, current.decodedSolution,
                    penaltyWeights, zoneOf, zonePenalty);
                if (rescored.evaluation.feasible)
                {
                    return rescored;
                }
                if (rescored.evaluation.penalizedCost < best.evaluation.penalizedCost)
                {
                    best = std::move(rescored);
                }
            }
            return best;
        }

        HgsRunResult runHgsImpl(
            const Instance &inst,
            const std::vector<Visit> &visitCatalog,
            const Solution &feasibleSeed,
            const HgsPenaltyWeights &initialPenalties,
            const HgsRunOptions &options,
            const std::vector<int> *zoneOf,
            double zonePenalty)
        {
            const std::size_t maxSize = std::numeric_limits<std::size_t>::max();
            if (options.mu == 0 || options.lambda == 0 ||
                options.mu > maxSize / 4 || options.lambda > maxSize - options.mu ||
                options.nClose == 0 || options.nElite > options.mu ||
                options.maxNonImprovingIterations == 0 ||
                !std::isfinite(options.repairProbability) ||
                options.repairProbability < 0.0 || options.repairProbability > 1.0)
            {
                throw std::invalid_argument("invalid HGS run options");
            }
            HgsPenaltyController controller(initialPenalties, options.penaltyControl);
            HgsPopulation population(visitCatalog.size());
            HgsIndividual seed = makeHgsIndividualFromSolutionImpl(
                inst, visitCatalog, feasibleSeed, initialPenalties, zoneOf, zonePenalty);
            if (!seed.evaluation.feasible || !validate(inst, feasibleSeed).feasible)
            {
                throw std::invalid_argument("HGS requires an independently validated feasible seed");
            }
            population.addIndividual(seed);
            HgsRunResult result;
            std::mt19937 rng(options.seed);
            std::uniform_real_distribution<double> drawProbability(0.0, 1.0);
            const auto trim = [&]()
            {
                population.selectSurvivors(
                    visitCatalog, options.mu, options.nClose, options.nElite,
                    options.mu + options.lambda, options.deadline);
            };
            const auto process = [&](HgsIndividual individual)
            {
                const HgsPenaltyWeights weights = controller.weights();
                individual = educateHgsIndividualImpl(
                    inst, visitCatalog, individual, weights, zoneOf, zonePenalty,
                    options.maxAcceptedEducationMoves, options.deadline);
                const HgsConstraintViolations observed = individual.evaluation.violations;
                bool improvesBest = population.addIndividual(individual);
                if (!individual.evaluation.feasible && !hgsDeadlineExpired(options.deadline) &&
                    drawProbability(rng) < options.repairProbability)
                {
                    ++result.repairsAttempted;
                    HgsIndividual repaired = repairHgsIndividualImpl(
                        inst, visitCatalog, individual, weights, zoneOf, zonePenalty,
                        options.maxAcceptedEducationMoves, options.deadline);
                    if (repaired.evaluation.feasible)
                    {
                        ++result.repairsSucceeded;
                        improvesBest = population.addIndividual(std::move(repaired)) || improvesBest;
                    }
                }
                if (controller.observe(observed))
                {
                    ++result.penaltyUpdates;
                    population.updatePenaltyWeights(controller.weights());
                }
                checkHgsDeadline(options.deadline);
                trim();
                return improvesBest;
            };
            const auto fill = [&]()
            {
                bool improved = false;
                for (std::size_t attempt = 0; attempt < 4 * options.mu; ++attempt)
                {
                    checkHgsDeadline(options.deadline);
                    ++result.randomTourAttempts;
                    auto tour = population.bestFeasible()->giantTour;
                    std::shuffle(tour.begin(), tour.end(), rng);
                    std::optional<HgsIndividual> decoded;
                    try
                    {
                        decoded = decodeGiantTourImpl(
                            inst, visitCatalog, tour, controller.weights(), zoneOf, zonePenalty, options.deadline);
                    }
                    catch (const HgsSplitFailure &)
                    {
                        ++result.splitFailures;
                        continue;
                    }
                    improved = process(std::move(*decoded)) || improved;
                }
                return improved;
            };

            try
            {
                if (visitCatalog.empty())
                {
                    result.stopReason = HgsStopReason::EmptyInstance;
                }
                else if (options.maxIterations > 0)
                {
                    checkHgsDeadline(options.deadline);
                    fill();
                    std::size_t nonImproving = 0;
                    for (std::size_t iteration = 0; iteration < options.maxIterations; ++iteration)
                    {
                        checkHgsDeadline(options.deadline);
                        ++result.iterations;
                        const auto &first = population.selectParent(
                            visitCatalog, options.nClose, options.nElite, rng, options.deadline);
                        const auto &second = population.selectParent(
                            visitCatalog, options.nClose, options.nElite, rng, options.deadline);
                        checkHgsDeadline(options.deadline);
                        const auto tour = orderedCrossover(first.giantTour, second.giantTour, rng);
                        bool improved = false;
                        std::optional<HgsIndividual> decoded;
                        try
                        {
                            decoded = decodeGiantTourImpl(
                                inst, visitCatalog, tour, controller.weights(), zoneOf, zonePenalty, options.deadline);
                        }
                        catch (const HgsSplitFailure &)
                        {
                            ++result.splitFailures;
                        }
                        if (decoded)
                        {
                            improved = process(std::move(*decoded));
                        }
                        if (improved)
                        {
                            nonImproving = 0;
                        }
                        else
                        {
                            ++nonImproving;
                        }
                        if (nonImproving >= options.maxNonImprovingIterations)
                        {
                            result.stopReason = HgsStopReason::StagnationLimit;
                            break;
                        }
                        if (iteration + 1 < options.maxIterations &&
                            options.diversificationInterval > 0 &&
                            nonImproving > 0 &&
                            nonImproving % options.diversificationInterval == 0)
                        {
                            checkHgsDeadline(options.deadline);
                            ++result.diversifications;
                            population.retainBest(std::max<std::size_t>(1, options.mu / 3));
                            if (fill())
                            {
                                nonImproving = 0;
                            }
                        }
                    }
                }
            }
            catch (const HgsTimeLimitReached &)
            {
                result.stopReason = HgsStopReason::TimeLimit;
            }
            result.bestFeasible = *population.bestFeasible();
            result.finalPenaltyWeights = controller.weights();
            if (!validate(inst, result.bestFeasible.decodedSolution).feasible)
            {
                throw std::logic_error("HGS returned an invalid feasible incumbent");
            }
            return result;
        }
    }

    std::vector<HgsGene> makeCanonicalGiantTour(
        std::size_t visitCount)
    {
        std::vector<HgsGene> giantTour;
        giantTour.reserve(visitCount);

        for (std::size_t visitIndex = 0;
             visitIndex < visitCount;
             ++visitIndex)
        {
            giantTour.push_back({visitIndex});
        }

        return giantTour;
    }

    std::vector<HgsGene> orderedCrossover(
        const std::vector<HgsGene> &firstParent,
        const std::vector<HgsGene> &secondParent,
        std::size_t beginIndex,
        std::size_t endIndex)
    {
        validateCrossoverParents(firstParent, secondParent);
        const std::size_t count = firstParent.size();
        if (beginIndex > endIndex || endIndex > count ||
            (count > 0 && beginIndex == endIndex))
        {
            throw std::invalid_argument(
                "OX segment must be a non-empty half-open range");
        }
        if (count == 0)
        {
            return {};
        }

        std::vector<HgsGene> offspring(count);
        std::vector<bool> used(count, false);
        for (std::size_t index = beginIndex; index < endIndex; ++index)
        {
            offspring[index] = firstParent[index];
            used[firstParent[index].visitIndex] = true;
        }

        std::size_t readPosition = endIndex % count;
        std::size_t writePosition = endIndex % count;
        for (std::size_t scanned = 0; scanned < count; ++scanned)
        {
            const HgsGene gene = secondParent[readPosition];
            if (!used[gene.visitIndex])
            {
                offspring[writePosition] = gene;
                used[gene.visitIndex] = true;
                writePosition = (writePosition + 1) % count;
                if (writePosition == beginIndex)
                {
                    writePosition = endIndex % count;
                }
            }
            readPosition = (readPosition + 1) % count;
        }
        return offspring;
    }

    std::vector<HgsGene> orderedCrossover(
        const std::vector<HgsGene> &firstParent,
        const std::vector<HgsGene> &secondParent,
        std::mt19937 &rng)
    {
        validateCrossoverParents(firstParent, secondParent);
        if (firstParent.empty())
        {
            return {};
        }

        std::uniform_int_distribution<std::size_t> draw(
            0, firstParent.size() - 1);
        const std::size_t firstCut = draw(rng);
        const std::size_t secondCut = draw(rng);
        return orderedCrossover(
            firstParent, secondParent,
            std::min(firstCut, secondCut),
            std::max(firstCut, secondCut) + 1);
    }

    std::vector<HgsGene> encodeSolutionAsGiantTour(
        const Solution &solution,
        const std::vector<Visit> &visitCatalog)
    {
        std::map<std::pair<int, int>, std::size_t>
            visitIndexByIdentity;

        for (std::size_t visitIndex = 0;
             visitIndex < visitCatalog.size();
             ++visitIndex)
        {
            const Visit &visit = visitCatalog[visitIndex];
            const auto [it, inserted] =
                visitIndexByIdentity.emplace(
                    std::make_pair(
                        visit.nodeIndex,
                        visit.chunkIdx),
                    visitIndex);

            if (!inserted)
            {
                throw std::invalid_argument(
                    "HGS visit catalog contains duplicate "
                    "(nodeIndex, chunkIdx)");
            }
        }

        std::vector<HgsGene> giantTour;
        giantTour.reserve(visitCatalog.size());

        for (const Route &route : solution.routes)
        {
            for (const Visit &stop : route.stops)
            {
                const auto it = visitIndexByIdentity.find(
                    std::make_pair(
                        stop.nodeIndex,
                        stop.chunkIdx));

                if (it == visitIndexByIdentity.end())
                {
                    throw std::invalid_argument(
                        "solution contains a visit that is "
                        "not in the HGS visit catalog");
                }

                giantTour.push_back({it->second});
            }
        }

        if (!isCompleteGiantTourPermutation(
                giantTour, visitCatalog.size()))
        {
            throw std::invalid_argument(
                "solution must contain every HGS visit "
                "exactly once");
        }

        return giantTour;
    }

    HgsRouteSegmentEvaluation evaluateGiantTourSegment(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        std::size_t beginIndex,
        std::size_t endIndex)
    {
        return evaluateGiantTourSegmentImpl(
            inst,
            visitCatalog,
            giantTour,
            beginIndex,
            endIndex,
            nullptr);
    }

    HgsRouteSegmentEvaluation evaluateGiantTourSegment(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        std::size_t beginIndex,
        std::size_t endIndex,
        const std::vector<int> &zoneOf)
    {
        return evaluateGiantTourSegmentImpl(
            inst,
            visitCatalog,
            giantTour,
            beginIndex,
            endIndex,
            &zoneOf);
    }

    HgsIndividualEvaluation aggregateHgsIndividualEvaluation(
        const std::vector<HgsRouteSegmentEvaluation>
            &routeEvaluations,
        const HgsPenaltyWeights &penaltyWeights,
        double zonePenalty)
    {
        validatePenaltyWeights(penaltyWeights, zonePenalty);

        HgsIndividualEvaluation evaluation;

        for (const HgsRouteSegmentEvaluation
                 &routeEvaluation : routeEvaluations)
        {
            if (routeEvaluation.hasCustomerConflict)
            {
                throw std::invalid_argument(
                    "cannot aggregate an HGS route with "
                    "two chunks of the same customer");
            }

            evaluation.distanceCost +=
                routeEvaluation.distanceCost;
            evaluation.routeZoneExcess +=
                routeEvaluation.routeZoneExcess;
            evaluation.violations.weightExcess +=
                routeEvaluation.violations.weightExcess;
            evaluation.violations.volumeExcess +=
                routeEvaluation.violations.volumeExcess;
            evaluation.violations.timeWarp +=
                routeEvaluation.violations.timeWarp;
        }

        evaluation.routeZoneCost =
            zonePenalty *
            static_cast<double>(
                evaluation.routeZoneExcess);
        evaluation.objectiveCost =
            evaluation.distanceCost +
            evaluation.routeZoneCost;
        evaluation.penalizedCost =
            computePenalizedCost(
                evaluation.distanceCost,
                evaluation.routeZoneExcess,
                evaluation.violations,
                penaltyWeights,
                zonePenalty);

        evaluation.feasible =
            evaluation.violations.weightExcess <=
                kWeightTolerance &&
            evaluation.violations.volumeExcess <=
                kVolumeTolerance &&
            evaluation.violations.timeWarp <=
                kTimeTolerance;

        return evaluation;
    }

    HgsIndividual makeHgsIndividualFromSolution(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const Solution &solution,
        const HgsPenaltyWeights &penaltyWeights)
    {
        return makeHgsIndividualFromSolutionImpl(
            inst, visitCatalog, solution, penaltyWeights,
            nullptr, 0.0);
    }

    HgsIndividual makeHgsIndividualFromSolution(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const Solution &solution,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty)
    {
        return makeHgsIndividualFromSolutionImpl(
            inst, visitCatalog, solution, penaltyWeights,
            &zoneOf, zonePenalty);
    }

    HgsIndividual educateHgsRelocate(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        std::size_t maxAcceptedMoves,
        const HgsDeadline &deadline)
    {
        return educateHgsRelocateImpl(
            inst, visitCatalog, individual, penaltyWeights,
            nullptr, 0.0, maxAcceptedMoves, deadline);
    }

    HgsIndividual educateHgsRelocate(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty,
        std::size_t maxAcceptedMoves,
        const HgsDeadline &deadline)
    {
        return educateHgsRelocateImpl(
            inst, visitCatalog, individual, penaltyWeights,
            &zoneOf, zonePenalty, maxAcceptedMoves, deadline);
    }

    HgsIndividual educateHgsIndividual(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        std::size_t maxAcceptedMoves,
        const HgsDeadline &deadline)
    {
        return educateHgsIndividualImpl(
            inst, visitCatalog, individual, penaltyWeights,
            nullptr, 0.0, maxAcceptedMoves, deadline);
    }

    HgsIndividual educateHgsIndividual(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty,
        std::size_t maxAcceptedMoves,
        const HgsDeadline &deadline)
    {
        return educateHgsIndividualImpl(
            inst, visitCatalog, individual, penaltyWeights,
            &zoneOf, zonePenalty, maxAcceptedMoves, deadline);
    }

    HgsIndividual repairHgsIndividual(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        std::size_t maxAcceptedMovesPerAttempt,
        const HgsDeadline &deadline)
    {
        return repairHgsIndividualImpl(
            inst, visitCatalog, individual, penaltyWeights,
            nullptr, 0.0, maxAcceptedMovesPerAttempt, deadline);
    }

    HgsIndividual repairHgsIndividual(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsIndividual &individual,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty,
        std::size_t maxAcceptedMovesPerAttempt,
        const HgsDeadline &deadline)
    {
        return repairHgsIndividualImpl(
            inst, visitCatalog, individual, penaltyWeights,
            &zoneOf, zonePenalty, maxAcceptedMovesPerAttempt, deadline);
    }

    HgsIndividual decodeGiantTour(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        const HgsPenaltyWeights &penaltyWeights,
        const HgsDeadline &deadline)
    {
        return decodeGiantTourImpl(
            inst,
            visitCatalog,
            giantTour,
            penaltyWeights,
            nullptr,
            0.0,
            deadline);
    }

    HgsIndividual decodeGiantTour(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsGene> &giantTour,
        const HgsPenaltyWeights &penaltyWeights,
        const std::vector<int> &zoneOf,
        double zonePenalty,
        const HgsDeadline &deadline)
    {
        return decodeGiantTourImpl(
            inst,
            visitCatalog,
            giantTour,
            penaltyWeights,
            &zoneOf,
            zonePenalty,
            deadline);
    }

    double directedBrokenPairsDistance(
        const std::vector<Visit> &visitCatalog,
        const Solution &first,
        const Solution &second,
        const HgsDeadline &deadline)
    {
        checkHgsDeadline(deadline);
        const HgsRouteAdjacency firstAdjacency =
            buildRouteAdjacency(visitCatalog, first, deadline);
        const HgsRouteAdjacency secondAdjacency =
            buildRouteAdjacency(visitCatalog, second, deadline);

        if (visitCatalog.empty())
        {
            return 0.0;
        }

        std::size_t brokenPairs = 0;
        for (std::size_t visitIndex = 0;
             visitIndex < visitCatalog.size();
             ++visitIndex)
        {
            checkHgsDeadline(deadline);
            brokenPairs +=
                firstAdjacency.predecessor[visitIndex] !=
                secondAdjacency.predecessor[visitIndex];
            brokenPairs +=
                firstAdjacency.successor[visitIndex] !=
                secondAdjacency.successor[visitIndex];
        }

        return static_cast<double>(brokenPairs) /
               (2.0 * static_cast<double>(visitCatalog.size()));
    }

    std::vector<HgsFitness> computeHgsFitness(
        const std::vector<Visit> &visitCatalog,
        const std::vector<HgsIndividual> &individuals,
        std::size_t nClose,
        std::size_t nElite,
        const HgsDeadline &deadline)
    {
        checkHgsDeadline(deadline);
        if (nClose == 0)
        {
            throw std::invalid_argument(
                "HGS fitness requires at least one closest neighbour");
        }

        const std::size_t count = individuals.size();
        if (count == 0)
        {
            return {};
        }

        std::vector<double> costs(count);
        std::vector<std::vector<double>> neighbourDistances(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            checkHgsDeadline(deadline);
            const HgsIndividual &individual = individuals[index];
            if (individual.evaluation.feasible !=
                individuals.front().evaluation.feasible)
            {
                throw std::invalid_argument(
                    "HGS fitness requires one feasibility subpopulation");
            }
            if (!isCompleteGiantTourPermutation(
                    individual.giantTour, visitCatalog.size()))
            {
                throw std::invalid_argument(
                    "HGS fitness requires complete giant tours");
            }

            costs[index] = individual.evaluation.penalizedCost;
            if (!std::isfinite(costs[index]) || costs[index] < 0.0)
            {
                throw std::invalid_argument(
                    "HGS fitness costs must be finite and non-negative");
            }
        }

        for (std::size_t first = 0; first < count; ++first)
        {
            checkHgsDeadline(deadline);
            for (std::size_t second = first + 1;
                 second < count; ++second)
            {
                checkHgsDeadline(deadline);
                const double distance = directedBrokenPairsDistance(
                    visitCatalog,
                    individuals[first].decodedSolution,
                    individuals[second].decodedSolution,
                    deadline);
                neighbourDistances[first].push_back(distance);
                neighbourDistances[second].push_back(distance);
            }
        }

        const std::size_t neighbours = std::min(nClose, count - 1);
        std::vector<double> diversity(count, 0.0);
        for (std::size_t index = 0; index < count; ++index)
        {
            checkHgsDeadline(deadline);
            auto &distances = neighbourDistances[index];
            std::sort(distances.begin(), distances.end());
            for (std::size_t closest = 0; closest < neighbours; ++closest)
            {
                checkHgsDeadline(deadline);
                diversity[index] += distances[closest];
            }
            if (neighbours > 0)
            {
                diversity[index] /= static_cast<double>(neighbours);
            }
        }

        const std::vector<double> costRanks =
            normalizedAverageRanks(costs, false);
        const std::vector<double> diversityRanks =
            normalizedAverageRanks(diversity, true);
        const double diversityWeight =
            1.0 - static_cast<double>(std::min(nElite, count)) /
                      static_cast<double>(count);

        std::vector<HgsFitness> fitness(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            checkHgsDeadline(deadline);
            fitness[index] = {
                diversity[index],
                costRanks[index],
                diversityRanks[index],
                costRanks[index] + diversityWeight * diversityRanks[index]};
        }
        checkHgsDeadline(deadline);
        return fitness;
    }

    HgsPenaltyWeights makeInitialHgsPenalties(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const HgsPenaltyControlOptions &controlOptions)
    {
        const HgsPenaltyController checkedBounds(
            {controlOptions.minPenalty, controlOptions.minPenalty, controlOptions.minPenalty},
            controlOptions);
        double distanceScale = 0.0;
        double weightScale = 0.0;
        double volumeScale = 0.0;
        double timeScale = 0.0;
        const auto accumulate = [](double &maximum, double value)
        {
            if (!std::isfinite(value) || value < 0.0)
            {
                throw std::invalid_argument("HGS penalty scales require finite non-negative data");
            }
            maximum = std::max(maximum, value);
        };
        for (const Visit &visit : visitCatalog)
        {
            accumulate(weightScale, visit.weight);
            accumulate(volumeScale, visit.volume);
        }
        for (const auto &row : inst.distanceMatrix)
        {
            for (double distance : row)
            {
                accumulate(distanceScale, distance);
            }
        }
        for (const auto &row : inst.durationMatrix)
        {
            for (double duration : row)
            {
                accumulate(timeScale, duration);
            }
        }
        for (const Node &node : inst.nodes)
        {
            accumulate(timeScale, static_cast<double>(node.serviceTime));
        }
        if (distanceScale == 0.0) { distanceScale = 1.0; }
        if (weightScale == 0.0) { weightScale = inst.fleet.weightCapacity; }
        if (volumeScale == 0.0) { volumeScale = inst.fleet.volumeCapacity; }
        if (timeScale == 0.0) { timeScale = inst.horizon; }
        const auto penalty = [&](double scale)
        {
            if (!std::isfinite(scale) || scale <= 0.0)
            {
                throw std::invalid_argument("HGS penalty denominator must be finite and positive");
            }
            return std::clamp(
                distanceScale / scale, controlOptions.minPenalty, controlOptions.maxPenalty);
        };
        return {penalty(weightScale), penalty(volumeScale), penalty(timeScale)};
    }

    HgsPenaltyController::HgsPenaltyController(
        HgsPenaltyWeights initialWeights,
        HgsPenaltyControlOptions options)
        : weights_(initialWeights), options_(options)
    {
        validatePenaltyWeights(weights_, 0.0);
        for (double value : {
                 options.targetFeasible, options.tolerance,
                 options.increaseFactor, options.decreaseFactor,
                 options.minPenalty, options.maxPenalty})
        {
            if (!std::isfinite(value))
            {
                throw std::invalid_argument("penalty control options must be finite");
            }
        }
        if (options.windowSize == 0 ||
            options.targetFeasible <= 0.0 || options.targetFeasible >= 1.0 ||
            options.tolerance < 0.0 ||
            options.targetFeasible - options.tolerance < 0.0 ||
            options.targetFeasible + options.tolerance > 1.0 ||
            options.increaseFactor <= 1.0 ||
            options.decreaseFactor <= 0.0 || options.decreaseFactor >= 1.0 ||
            options.minPenalty <= 0.0 || options.maxPenalty < options.minPenalty)
        {
            throw std::invalid_argument("invalid HGS penalty control options");
        }
        for (double value : {
                 weights_.weightPenalty, weights_.volumePenalty,
                 weights_.timeWarpPenalty})
        {
            if (value < options.minPenalty || value > options.maxPenalty)
            {
                throw std::invalid_argument("initial adaptive penalties must be within bounds");
            }
        }
    }

    bool HgsPenaltyController::observe(const HgsConstraintViolations &violations)
    {
        for (double value : {
                 violations.weightExcess, violations.volumeExcess, violations.timeWarp})
        {
            if (!std::isfinite(value) || value < 0.0)
            {
                throw std::invalid_argument("observed violations must be finite and non-negative");
            }
        }
        weightFeasible_ += violations.weightExcess <= kWeightTolerance;
        volumeFeasible_ += violations.volumeExcess <= kVolumeTolerance;
        timeFeasible_ += violations.timeWarp <= kTimeTolerance;
        if (++observations_ < options_.windowSize)
        {
            return false;
        }

        const auto adjust = [&](double value, std::size_t feasibleCount)
        {
            const double rate =
                static_cast<double>(feasibleCount) / static_cast<double>(observations_);
            if (rate + 1e-12 < options_.targetFeasible - options_.tolerance)
            {
                return value >= options_.maxPenalty / options_.increaseFactor
                           ? options_.maxPenalty : value * options_.increaseFactor;
            }
            if (rate > options_.targetFeasible + options_.tolerance + 1e-12)
            {
                return std::max(options_.minPenalty, value * options_.decreaseFactor);
            }
            return value;
        };
        const HgsPenaltyWeights next = {
            adjust(weights_.weightPenalty, weightFeasible_),
            adjust(weights_.volumePenalty, volumeFeasible_),
            adjust(weights_.timeWarpPenalty, timeFeasible_)};
        const bool changed =
            next.weightPenalty != weights_.weightPenalty ||
            next.volumePenalty != weights_.volumePenalty ||
            next.timeWarpPenalty != weights_.timeWarpPenalty;
        weights_ = next;
        observations_ = weightFeasible_ = volumeFeasible_ = timeFeasible_ = 0;
        return changed;
    }

    const HgsPenaltyWeights &HgsPenaltyController::weights() const
    {
        return weights_;
    }

    void HgsPopulation::updatePenaltyWeights(const HgsPenaltyWeights &penaltyWeights)
    {
        validatePenaltyWeights(penaltyWeights, 0.0);
        const auto score = [&](const HgsIndividual &individual)
        {
            const double value = computePenalizedCost(
                individual.evaluation.objectiveCost, 0,
                individual.evaluation.violations, penaltyWeights, 0.0);
            if (!std::isfinite(value))
            {
                throw std::overflow_error("HGS population penalty score overflow");
            }
            return value;
        };
        const auto scores = [&](const std::vector<HgsIndividual> &members)
        {
            std::vector<double> values;
            values.reserve(members.size());
            for (const auto &member : members)
            {
                values.push_back(score(member));
            }
            return values;
        };
        const auto feasibleScores = scores(feasibleIndividuals_);
        const auto infeasibleScores = scores(infeasibleIndividuals_);
        const double bestScore = bestFeasible_ ? score(*bestFeasible_) : 0.0;
        for (std::size_t index = 0; index < feasibleIndividuals_.size(); ++index)
        {
            feasibleIndividuals_[index].evaluation.penalizedCost = feasibleScores[index];
        }
        for (std::size_t index = 0; index < infeasibleIndividuals_.size(); ++index)
        {
            infeasibleIndividuals_[index].evaluation.penalizedCost = infeasibleScores[index];
        }
        if (bestFeasible_)
        {
            bestFeasible_->evaluation.penalizedCost = bestScore;
        }
    }

    HgsPopulation::HgsPopulation(std::size_t visitCount)
        : visitCount_(visitCount)
    {
    }

    bool HgsPopulation::addIndividual(HgsIndividual individual)
    {
        if (!isCompleteGiantTourPermutation(
                individual.giantTour, visitCount_))
        {
            throw std::invalid_argument(
                "population individual must have a complete giant tour");
        }

        const HgsIndividualEvaluation &evaluation =
            individual.evaluation;
        const HgsConstraintViolations &violations =
            evaluation.violations;

        for (double value :
             {evaluation.objectiveCost,
              evaluation.penalizedCost,
              violations.weightExcess,
              violations.volumeExcess,
              violations.timeWarp})
        {
            if (!std::isfinite(value) || value < 0.0)
            {
                throw std::invalid_argument(
                    "population scores and violations must be finite "
                    "and non-negative");
            }
        }

        const bool measuredFeasible =
            violations.weightExcess <= kWeightTolerance &&
            violations.volumeExcess <= kVolumeTolerance &&
            violations.timeWarp <= kTimeTolerance;

        if (evaluation.feasible != measuredFeasible)
        {
            throw std::invalid_argument(
                "population feasibility flag disagrees with violations");
        }

        if (!evaluation.feasible)
        {
            infeasibleIndividuals_.push_back(std::move(individual));
            return false;
        }

        const bool improvesBest =
            !bestFeasible_.has_value() ||
            evaluation.objectiveCost <
                bestFeasible_->evaluation.objectiveCost;

        feasibleIndividuals_.push_back(std::move(individual));
        if (improvesBest)
        {
            bestFeasible_ = feasibleIndividuals_.back();
        }
        return improvesBest;
    }

    std::size_t HgsPopulation::selectSurvivors(
        const std::vector<Visit> &visitCatalog,
        std::size_t targetSize,
        std::size_t nClose,
        std::size_t nElite,
        std::size_t triggerSize,
        const HgsDeadline &deadline)
    {
        checkHgsDeadline(deadline);
        if (visitCatalog.size() != visitCount_)
        {
            throw std::invalid_argument(
                "survivor selection requires the population visit catalog");
        }
        if (targetSize == 0 || nClose == 0 ||
            (triggerSize != 0 && triggerSize <= targetSize))
        {
            throw std::invalid_argument(
                "invalid survivor target, nClose or trigger size");
        }

        const std::size_t feasibleRemoved = feasibleIndividuals_.size() >= triggerSize
            ? trimHgsSubpopulation(visitCatalog, feasibleIndividuals_, targetSize, nClose, nElite, deadline)
            : 0;
        const std::size_t infeasibleRemoved = infeasibleIndividuals_.size() >= triggerSize
            ? trimHgsSubpopulation(visitCatalog, infeasibleIndividuals_, targetSize, nClose, nElite, deadline)
            : 0;
        return feasibleRemoved + infeasibleRemoved;
    }

    void HgsPopulation::retainBest(std::size_t targetSize)
    {
        if (targetSize == 0)
        {
            throw std::invalid_argument("HGS retention requires a positive target size");
        }
        const auto retain = [&](std::vector<HgsIndividual> &members)
        {
            std::stable_sort(
                members.begin(), members.end(),
                [](const HgsIndividual &first, const HgsIndividual &second)
                {
                    return first.evaluation.penalizedCost < second.evaluation.penalizedCost;
                });
            if (members.size() > targetSize)
            {
                members.erase(
                    members.begin() + static_cast<std::ptrdiff_t>(targetSize), members.end());
            }
        };
        retain(feasibleIndividuals_);
        retain(infeasibleIndividuals_);
    }

    const HgsIndividual &HgsPopulation::selectParent(
        const std::vector<Visit> &visitCatalog,
        std::size_t nClose,
        std::size_t nElite,
        std::mt19937 &rng,
        const HgsDeadline &deadline) const
    {
        checkHgsDeadline(deadline);
        if (visitCatalog.size() != visitCount_)
        {
            throw std::invalid_argument(
                "parent selection requires the population visit catalog");
        }
        const std::size_t feasibleCount = feasibleIndividuals_.size();
        const std::size_t totalCount =
            feasibleCount + infeasibleIndividuals_.size();
        if (totalCount == 0)
        {
            throw std::invalid_argument(
                "parent selection requires a non-empty population");
        }

        const std::vector<HgsFitness> feasibleFitness = computeHgsFitness(
            visitCatalog, feasibleIndividuals_, nClose, nElite, deadline);
        const std::vector<HgsFitness> infeasibleFitness = computeHgsFitness(
            visitCatalog, infeasibleIndividuals_, nClose, nElite, deadline);
        const auto fitnessAt = [&](std::size_t index)
        {
            return index < feasibleCount
                       ? feasibleFitness[index].biasedFitness
                       : infeasibleFitness[index - feasibleCount].biasedFitness;
        };

        checkHgsDeadline(deadline);
        std::uniform_int_distribution<std::size_t> draw(0, totalCount - 1);
        const std::size_t first = draw(rng);
        const std::size_t second = draw(rng);
        const std::size_t winner =
            fitnessAt(first) <= fitnessAt(second) ? first : second;
        return winner < feasibleCount
                   ? feasibleIndividuals_[winner]
                   : infeasibleIndividuals_[winner - feasibleCount];
    }

    const std::vector<HgsIndividual> &
    HgsPopulation::feasibleIndividuals() const
    {
        return feasibleIndividuals_;
    }

    const std::vector<HgsIndividual> &
    HgsPopulation::infeasibleIndividuals() const
    {
        return infeasibleIndividuals_;
    }

    const std::optional<HgsIndividual> &
    HgsPopulation::bestFeasible() const
    {
        return bestFeasible_;
    }

    HgsRunResult runHgs(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const Solution &feasibleSeed,
        const HgsPenaltyWeights &initialPenalties,
        const HgsRunOptions &options)
    {
        return runHgsImpl(
            inst, visitCatalog, feasibleSeed, initialPenalties, options, nullptr, 0.0);
    }

    HgsRunResult runHgs(
        const Instance &inst,
        const std::vector<Visit> &visitCatalog,
        const Solution &feasibleSeed,
        const HgsPenaltyWeights &initialPenalties,
        const HgsRunOptions &options,
        const std::vector<int> &zoneOf,
        double zonePenalty)
    {
        return runHgsImpl(
            inst, visitCatalog, feasibleSeed, initialPenalties, options, &zoneOf, zonePenalty);
    }

    bool isCompleteGiantTourPermutation(
        const std::vector<HgsGene> &giantTour,
        std::size_t visitCount)
    {
        if (giantTour.size() != visitCount)
        {
            return false;
        }

        std::vector<bool> seen(visitCount, false);

        for (const HgsGene gene : giantTour)
        {
            if (gene.visitIndex >= visitCount ||
                seen[gene.visitIndex])
            {
                return false;
            }

            seen[gene.visitIndex] = true;
        }

        return true;
    }
}
