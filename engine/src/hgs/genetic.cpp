#include "router/hgs/genetic.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <utility>

#include "router/hgs/crossover.hpp"
#include "router/hgs/initial_population.hpp"
#include "router/hgs/split.hpp"

namespace router::hgs
{
    namespace
    {
        bool isFraction(double value)
        {
            return std::isfinite(value) && value >= 0.0 && value <= 1.0;
        }

        Penalties scaled(const Penalties &penalties, double factor)
        {
            return {penalties.weight * factor, penalties.volume * factor, penalties.timeWarp * factor};
        }

        class Genetic
        {
        public:
            Genetic(const ProblemData &data, const GeneticConfig &config, Deadline deadline, const TraceSink &trace)
                : data_(data),
                  config_(config),
                  deadline_(std::move(deadline)),
                  trace_(trace),
                  rng_(config.seed),
                  controller_(initialPenalties(data, config.penaltyControl), config.penaltyControl),
                  population_(data, config.population, controller_.penalties()),
                  search_(data, makeMoves(config.moves)),
                  start_(SteadyClock::now())
            {
                search_.setInvariantChecks(config.checkInvariants);
                result_.initialPenalties = controller_.penalties();
            }

            GeneticResult run(const Individual &seed)
            {
                population_.add(seed);
                if (data_.visitCount() == 0)
                {
                    result_.stopReason = StopReason::EmptyInstance;
                    return finish();
                }
                if (deadline_.expired())
                {
                    result_.stopReason = StopReason::TimeLimit;
                    return finish();
                }

                fill();
                std::size_t nonImproving = 0;
                result_.stopReason = StopReason::IterationLimit;
                while (result_.stats.iterations < config_.maxIterations)
                {
                    if (deadline_.expired())
                    {
                        result_.stopReason = StopReason::TimeLimit;
                        break;
                    }
                    ++result_.stats.iterations;
                    const bool improved = iterate();
                    nonImproving = improved ? 0 : nonImproving + 1;
                    if (nonImproving >= config_.maxNonImprovingIterations)
                    {
                        result_.stopReason = StopReason::StagnationLimit;
                        break;
                    }
                    if (config_.diversificationInterval > 0 && nonImproving > 0 &&
                        nonImproving % config_.diversificationInterval == 0 &&
                        result_.stats.iterations < config_.maxIterations)
                    {
                        ++result_.stats.diversifications;
                        population_.retainBest(std::max<std::size_t>(1, config_.population.mu / 3));
                        if (fill())
                        {
                            nonImproving = 0;
                        }
                    }
                    if (config_.traceInterval > 0 && result_.stats.iterations % config_.traceInterval == 0)
                    {
                        emitTrace();
                    }
                }
                return finish();
            }

        private:
            bool iterate()
            {
                std::vector<int> tour;
                {
                    std::pair<const Individual *, const Individual *> parents;
                    {
                        ScopedTimer timer(result_.stats.parentSelection);
                        parents = population_.selectParents(rng_);
                    }
                    ScopedTimer timer(result_.stats.crossover);
                    tour = orderedCrossover(parents.first->giantTour, parents.second->giantTour, rng_);
                }
                return educateAndInsert(tour);
            }

            bool fill()
            {
                ScopedTimer timer(result_.stats.fill);
                Deadline fillDeadline = deadline_.isSet()
                                            ? deadline_.withinFractionOfRemaining(config_.fillTimeFraction)
                                            : Deadline{};
                const std::size_t attempts = 4 * config_.population.mu;
                const auto perturbedAttempts = static_cast<std::size_t>(
                    std::llround(config_.perturbedFillFraction * static_cast<double>(attempts)));
                bool improved = false;
                for (std::size_t attempt = 0; attempt < attempts; ++attempt)
                {
                    if (deadline_.expired())
                    {
                        break;
                    }
                    if (attempt > 0 && fillDeadline.expired())
                    {
                        ++result_.stats.fillsCutByTime;
                        break;
                    }
                    const std::vector<int> &incumbent = population_.bestFeasible()->giantTour;
                    std::vector<int> tour;
                    if (attempt < perturbedAttempts)
                    {
                        ++result_.stats.perturbedFillAttempts;
                        tour = perturbedTour(incumbent, config_.perturbationStrength, rng_);
                    }
                    else
                    {
                        ++result_.stats.randomFillAttempts;
                        tour = randomTour(incumbent, rng_);
                    }
                    improved = educateAndInsert(tour) || improved;
                }
                return improved;
            }

            bool educateAndInsert(const std::vector<int> &tour)
            {
                std::optional<Individual> child;
                {
                    ScopedTimer timer(result_.stats.split);
                    child = split(data_, tour, controller_.penalties());
                }
                if (!child)
                {
                    ++result_.stats.splitFailures;
                    return false;
                }

                const Penalties penalties = controller_.penalties();
                {
                    ScopedTimer timer(result_.stats.education);
                    search_.run(*child, penalties, rng_, deadline_);
                }
                bool improved = insert(*child);

                if (!isFeasible(child->cost) && !deadline_.expired() &&
                    std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < config_.repairProbability)
                {
                    ++result_.stats.repairsAttempted;
                    Individual repaired = *child;
                    {
                        ScopedTimer timer(result_.stats.repair);
                        search_.run(repaired, scaled(penalties, config_.repairPenaltyFactor), rng_, deadline_);
                    }
                    if (isFeasible(repaired.cost))
                    {
                        ++result_.stats.repairsSucceeded;
                        improved = insert(std::move(repaired)) || improved;
                    }
                }

                if (controller_.observe(child->cost))
                {
                    ++result_.stats.penaltyUpdates;
                    population_.setPenalties(controller_.penalties());
                }
                return improved;
            }

            bool insert(Individual individual)
            {
                ScopedTimer timer(result_.stats.populationUpdate);
                const bool improved = population_.add(std::move(individual));
                if (improved)
                {
                    result_.stats.iterationOfBest = result_.stats.iterations;
                    result_.stats.msToBest = elapsedMs();
                }
                return improved;
            }

            void emitTrace()
            {
                if (!trace_)
                {
                    return;
                }
                TraceRecord record;
                record.iteration = result_.stats.iterations;
                record.elapsedMs = elapsedMs();
                if (population_.bestFeasible())
                {
                    record.bestObjective = objectiveCost(data_, population_.bestFeasible()->cost);
                }
                record.feasibleSize = population_.feasible().size();
                record.infeasibleSize = population_.infeasible().size();
                record.penalties = controller_.penalties();
                trace_(record);
            }

            double elapsedMs() const
            {
                return std::chrono::duration<double, std::milli>(SteadyClock::now() - start_).count();
            }

            GeneticResult finish()
            {
                emitTrace();
                result_.best = *population_.bestFeasible();
                result_.finalPenalties = controller_.penalties();
                result_.localSearch = search_.stats();
                for (const auto &move : search_.moves())
                {
                    result_.moves.emplace_back(std::string(move->name()), move->stats());
                }
                result_.population = population_.stats();
                return std::move(result_);
            }

            const ProblemData &data_;
            const GeneticConfig &config_;
            Deadline deadline_;
            const TraceSink &trace_;
            Rng rng_;
            PenaltyController controller_;
            Population population_;
            LocalSearch search_;
            SteadyClock::time_point start_;
            GeneticResult result_;
        };
    }

    const char *stopReasonName(StopReason reason)
    {
        switch (reason)
        {
        case StopReason::IterationLimit: return "iteration_limit";
        case StopReason::StagnationLimit: return "stagnation_limit";
        case StopReason::TimeLimit: return "time_limit";
        case StopReason::EmptyInstance: return "empty_instance";
        }
        throw std::logic_error("unknown stop reason");
    }

    void validateGeneticConfig(const GeneticConfig &config)
    {
        validatePopulationOptions(config.population);
        validatePenaltyControlOptions(config.penaltyControl);
        makeMoves(config.moves);
        if (config.maxNonImprovingIterations == 0 ||
            !isFraction(config.repairProbability) ||
            !std::isfinite(config.repairPenaltyFactor) || config.repairPenaltyFactor < 1.0 ||
            !isFraction(config.perturbedFillFraction) ||
            !isFraction(config.perturbationStrength) ||
            !isFraction(config.fillTimeFraction) || config.fillTimeFraction == 0.0)
        {
            throw std::invalid_argument("invalid genetic search configuration");
        }
    }

    GeneticResult runGenetic(
        const ProblemData &data,
        const Individual &feasibleSeed,
        const GeneticConfig &config,
        Deadline deadline,
        const TraceSink &trace)
    {
        validateGeneticConfig(config);
        if (!isFeasible(feasibleSeed.cost) || !isCompletePermutation(feasibleSeed.giantTour, data.visitCount()))
        {
            throw std::invalid_argument("genetic search requires a complete feasible seed");
        }
        Genetic genetic(data, config, std::move(deadline), trace);
        return genetic.run(feasibleSeed);
    }
}
