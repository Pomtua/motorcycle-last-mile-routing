#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/individual.hpp"
#include "router/hgs/moves.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/hgs/search_context.hpp"
#include "router/hgs/search_routes.hpp"

namespace router::hgs
{
    inline constexpr std::size_t kLocalSearchDeadlineInterval = 16;

    struct LocalSearchStats
    {
        std::size_t runs = 0;
        std::size_t passes = 0;
        std::size_t pairsSkipped = 0;
        std::size_t interruptedByDeadline = 0;
        StageStats time;
    };

    class LocalSearch
    {
    public:
        LocalSearch(const ProblemData &data, std::vector<std::unique_ptr<Move>> moves);
        LocalSearch(const LocalSearch &) = delete;
        LocalSearch &operator=(const LocalSearch &) = delete;

        void run(Individual &individual, const Penalties &penalties, Rng &rng, Deadline &deadline);
        void setInvariantChecks(bool enabled) { checkInvariants_ = enabled; }

        const LocalSearchStats &stats() const { return stats_; }
        const std::vector<std::unique_ptr<Move>> &moves() const { return moves_; }

    private:
        bool tryMoves(SearchNode &u, SearchNode &v);

        const ProblemData &data_;
        std::vector<std::unique_ptr<Move>> moves_;
        SearchRoutes routes_;
        std::vector<int> order_;
        std::vector<std::uint64_t> lastTested_;
        LocalSearchStats stats_;
        bool checkInvariants_ = false;
    };
}
