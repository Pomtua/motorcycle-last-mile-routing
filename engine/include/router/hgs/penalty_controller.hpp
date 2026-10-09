#pragma once

#include <cstddef>

#include "router/hgs/cost_model.hpp"
#include "router/hgs/problem_data.hpp"

namespace router::hgs
{
    struct PenaltyControlOptions
    {
        std::size_t windowSize = 100;
        double targetFeasible = 0.20;
        double tolerance = 0.05;
        double increaseFactor = 1.20;
        double decreaseFactor = 0.85;
        double minPenalty = 1e-6;
        double maxPenalty = 1e12;
    };

    void validatePenaltyControlOptions(const PenaltyControlOptions &options);

    Penalties initialPenalties(const ProblemData &data, const PenaltyControlOptions &options = {});

    class PenaltyController
    {
    public:
        PenaltyController(const Penalties &initial, const PenaltyControlOptions &options);

        bool observe(const CostBreakdown &cost);
        const Penalties &penalties() const { return penalties_; }
        std::size_t updates() const { return updates_; }

    private:
        Penalties penalties_;
        PenaltyControlOptions options_;
        std::size_t observations_ = 0;
        std::size_t weightFeasible_ = 0;
        std::size_t volumeFeasible_ = 0;
        std::size_t timeFeasible_ = 0;
        std::size_t updates_ = 0;
    };
}
