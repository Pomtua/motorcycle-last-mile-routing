#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/search_context.hpp"

namespace router::hgs
{
    inline constexpr double kCloneDistance = 1e-9;

    struct PopulationOptions
    {
        std::size_t mu = 25;
        std::size_t lambda = 40;
        std::size_t nClose = 5;
        std::size_t nElite = 4;
    };

    struct PopulationStats
    {
        std::size_t added = 0;
        std::size_t distancesComputed = 0;
        std::size_t survivorSelections = 0;
        std::size_t removedClones = 0;
        std::size_t removedByFitness = 0;
    };

    struct RemovalChoice
    {
        std::size_t index = 0;
        bool clone = false;
    };

    void validatePopulationOptions(const PopulationOptions &options);
    double brokenPairsDistance(const Individual &first, const Individual &second);
    std::vector<double> averageRanks(const std::vector<double> &values, bool higherIsBetter);

    class Subpopulation
    {
    public:
        std::size_t size() const { return members_.size(); }
        const Individual &member(std::size_t index) const { return members_[index]; }
        double distance(std::size_t first, std::size_t second) const { return distances_[first][second]; }

        void add(Individual individual, PopulationStats &stats);
        void remove(std::size_t index);
        std::vector<double> biasedFitness(const ProblemData &data, const Penalties &penalties, const PopulationOptions &options) const;
        RemovalChoice worstMember(const ProblemData &data, const Penalties &penalties, const PopulationOptions &options) const;

    private:
        std::vector<Individual> members_;
        std::vector<std::vector<double>> distances_;
    };

    class Population
    {
    public:
        Population(const ProblemData &data, const PopulationOptions &options, const Penalties &penalties);

        bool add(Individual individual);
        void setPenalties(const Penalties &penalties);
        std::pair<const Individual *, const Individual *> selectParents(Rng &rng) const;
        void retainBest(std::size_t count);

        std::size_t size() const { return feasible_.size() + infeasible_.size(); }
        const Subpopulation &feasible() const { return feasible_; }
        const Subpopulation &infeasible() const { return infeasible_; }
        const std::optional<Individual> &bestFeasible() const { return bestFeasible_; }
        const Penalties &penalties() const { return penalties_; }
        const PopulationStats &stats() const { return stats_; }

    private:
        void selectSurvivors(Subpopulation &subpopulation);

        const ProblemData &data_;
        PopulationOptions options_;
        Penalties penalties_;
        Subpopulation feasible_;
        Subpopulation infeasible_;
        std::optional<Individual> bestFeasible_;
        PopulationStats stats_;
    };
}
