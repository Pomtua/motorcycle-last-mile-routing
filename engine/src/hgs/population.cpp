#include "router/hgs/population.hpp"

#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>

namespace router::hgs
{
    void validatePopulationOptions(const PopulationOptions &options)
    {
        if (options.mu == 0 || options.lambda == 0 || options.nClose == 0 || options.nElite > options.mu)
        {
            throw std::invalid_argument("population needs positive mu, lambda and nClose, and nElite <= mu");
        }
    }

    double brokenPairsDistance(const Individual &first, const Individual &second)
    {
        if (first.successor.size() != second.successor.size() ||
            first.predecessor.size() != second.predecessor.size() ||
            first.successor.size() != first.predecessor.size())
        {
            throw std::invalid_argument("broken-pairs distance needs individuals of the same problem");
        }
        const std::size_t visits = first.successor.size() > 0 ? first.successor.size() - 1 : 0;
        if (visits == 0)
        {
            return 0.0;
        }
        std::size_t broken = 0;
        for (std::size_t visit = 1; visit <= visits; ++visit)
        {
            broken += first.predecessor[visit] != second.predecessor[visit];
            broken += first.successor[visit] != second.successor[visit];
        }
        return static_cast<double>(broken) / (2.0 * static_cast<double>(visits));
    }

    std::vector<double> averageRanks(const std::vector<double> &values, bool higherIsBetter)
    {
        std::vector<std::size_t> order(values.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
                  { return higherIsBetter ? values[a] > values[b] : values[a] < values[b]; });

        std::vector<double> ranks(values.size());
        for (std::size_t begin = 0; begin < order.size();)
        {
            std::size_t end = begin + 1;
            while (end < order.size() && values[order[end]] == values[order[begin]])
            {
                ++end;
            }
            const double rank = (static_cast<double>(begin) + 1.0 + static_cast<double>(end)) /
                                (2.0 * static_cast<double>(values.size()));
            for (std::size_t position = begin; position < end; ++position)
            {
                ranks[order[position]] = rank;
            }
            begin = end;
        }
        return ranks;
    }

    void Subpopulation::add(Individual individual, PopulationStats &stats)
    {
        std::vector<double> row;
        row.reserve(members_.size() + 1);
        for (std::size_t index = 0; index < members_.size(); ++index)
        {
            const double distance = brokenPairsDistance(individual, members_[index]);
            row.push_back(distance);
            distances_[index].push_back(distance);
        }
        row.push_back(0.0);
        stats.distancesComputed += members_.size();
        distances_.push_back(std::move(row));
        members_.push_back(std::move(individual));
    }

    void Subpopulation::remove(std::size_t index)
    {
        members_.erase(members_.begin() + static_cast<std::ptrdiff_t>(index));
        distances_.erase(distances_.begin() + static_cast<std::ptrdiff_t>(index));
        for (auto &row : distances_)
        {
            row.erase(row.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    std::vector<double> Subpopulation::biasedFitness(
        const ProblemData &data,
        const Penalties &penalties,
        const PopulationOptions &options) const
    {
        const std::size_t count = members_.size();
        if (count == 0)
        {
            return {};
        }

        std::vector<double> costs(count);
        std::vector<double> diversity(count, 0.0);
        const std::size_t closest = std::min(options.nClose, count - 1);
        std::vector<double> others;
        for (std::size_t index = 0; index < count; ++index)
        {
            costs[index] = penalizedCost(data, members_[index].cost, penalties);
            if (closest == 0)
            {
                continue;
            }
            others.clear();
            for (std::size_t other = 0; other < count; ++other)
            {
                if (other != index)
                {
                    others.push_back(distances_[index][other]);
                }
            }
            std::partial_sort(others.begin(), others.begin() + static_cast<std::ptrdiff_t>(closest), others.end());
            diversity[index] = std::accumulate(others.begin(), others.begin() + static_cast<std::ptrdiff_t>(closest), 0.0) /
                               static_cast<double>(closest);
        }

        const std::vector<double> costRanks = averageRanks(costs, false);
        const std::vector<double> diversityRanks = averageRanks(diversity, true);
        const double diversityWeight =
            1.0 - static_cast<double>(std::min(options.nElite, count)) / static_cast<double>(count);
        std::vector<double> fitness(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            fitness[index] = costRanks[index] + diversityWeight * diversityRanks[index];
        }
        return fitness;
    }

    RemovalChoice Subpopulation::worstMember(
        const ProblemData &data,
        const Penalties &penalties,
        const PopulationOptions &options) const
    {
        if (members_.empty())
        {
            throw std::logic_error("cannot remove from an empty subpopulation");
        }
        const std::vector<double> fitness = biasedFitness(data, penalties, options);
        std::vector<bool> isClone(members_.size(), false);
        bool anyClone = false;
        for (std::size_t index = 0; index < members_.size(); ++index)
        {
            for (std::size_t other = 0; other < members_.size() && !isClone[index]; ++other)
            {
                isClone[index] = other != index && distances_[index][other] <= kCloneDistance;
            }
            anyClone = anyClone || isClone[index];
        }

        std::optional<std::size_t> worst;
        for (std::size_t index = 0; index < members_.size(); ++index)
        {
            if (anyClone && !isClone[index])
            {
                continue;
            }
            if (!worst || fitness[index] > fitness[*worst] ||
                (fitness[index] == fitness[*worst] &&
                 penalizedCost(data, members_[index].cost, penalties) >=
                     penalizedCost(data, members_[*worst].cost, penalties)))
            {
                worst = index;
            }
        }
        return {*worst, anyClone};
    }

    Population::Population(const ProblemData &data, const PopulationOptions &options, const Penalties &penalties)
        : data_(data), options_(options), penalties_(penalties)
    {
        validatePopulationOptions(options_);
        validatePenalties(penalties_);
    }

    bool Population::add(Individual individual)
    {
        ++stats_.added;
        const bool feasible = isFeasible(individual.cost);
        bool improvesBest = false;
        if (feasible &&
            (!bestFeasible_ || objectiveCost(data_, individual.cost) < objectiveCost(data_, bestFeasible_->cost)))
        {
            bestFeasible_ = individual;
            improvesBest = true;
        }

        Subpopulation &target = feasible ? feasible_ : infeasible_;
        target.add(std::move(individual), stats_);
        if (target.size() > options_.mu + options_.lambda)
        {
            selectSurvivors(target);
        }
        return improvesBest;
    }

    void Population::selectSurvivors(Subpopulation &subpopulation)
    {
        ++stats_.survivorSelections;
        while (subpopulation.size() > options_.mu)
        {
            const RemovalChoice choice = subpopulation.worstMember(data_, penalties_, options_);
            subpopulation.remove(choice.index);
            ++(choice.clone ? stats_.removedClones : stats_.removedByFitness);
        }
    }

    void Population::setPenalties(const Penalties &penalties)
    {
        validatePenalties(penalties);
        penalties_ = penalties;
    }

    std::pair<const Individual *, const Individual *> Population::selectParents(Rng &rng) const
    {
        const std::size_t feasibleCount = feasible_.size();
        const std::size_t total = size();
        if (total == 0)
        {
            throw std::logic_error("parent selection needs a non-empty population");
        }
        const std::vector<double> feasibleFitness = feasible_.biasedFitness(data_, penalties_, options_);
        const std::vector<double> infeasibleFitness = infeasible_.biasedFitness(data_, penalties_, options_);
        const auto fitnessAt = [&](std::size_t index)
        {
            return index < feasibleCount ? feasibleFitness[index] : infeasibleFitness[index - feasibleCount];
        };
        const auto memberAt = [&](std::size_t index) -> const Individual *
        {
            return index < feasibleCount ? &feasible_.member(index) : &infeasible_.member(index - feasibleCount);
        };

        std::uniform_int_distribution<std::size_t> draw(0, total - 1);
        const auto tournament = [&]()
        {
            const std::size_t a = draw(rng);
            const std::size_t b = draw(rng);
            return memberAt(fitnessAt(a) <= fitnessAt(b) ? a : b);
        };
        const Individual *first = tournament();
        const Individual *second = tournament();
        return {first, second};
    }

    void Population::retainBest(std::size_t count)
    {
        if (count == 0)
        {
            throw std::invalid_argument("retention needs a positive count");
        }
        for (Subpopulation *subpopulation : {&feasible_, &infeasible_})
        {
            std::vector<std::size_t> order(subpopulation->size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
                             { return penalizedCost(data_, subpopulation->member(a).cost, penalties_) <
                                      penalizedCost(data_, subpopulation->member(b).cost, penalties_); });
            if (order.size() <= count)
            {
                continue;
            }
            std::vector<std::size_t> dropped(order.begin() + static_cast<std::ptrdiff_t>(count), order.end());
            std::sort(dropped.rbegin(), dropped.rend());
            for (std::size_t index : dropped)
            {
                subpopulation->remove(index);
            }
        }
    }
}
