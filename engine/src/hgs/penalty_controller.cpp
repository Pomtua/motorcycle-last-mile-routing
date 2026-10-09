#include "router/hgs/penalty_controller.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace router::hgs
{
    void validatePenaltyControlOptions(const PenaltyControlOptions &options)
    {
        for (double value : {options.targetFeasible, options.tolerance, options.increaseFactor,
                             options.decreaseFactor, options.minPenalty, options.maxPenalty})
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
            throw std::invalid_argument("invalid penalty control options");
        }
    }

    Penalties initialPenalties(const ProblemData &data, const PenaltyControlOptions &options)
    {
        validatePenaltyControlOptions(options);
        double distanceScale = 0.0;
        double timeScale = 0.0;
        double weightScale = 0.0;
        double volumeScale = 0.0;
        for (int from = 0; from <= static_cast<int>(data.visitCount()); ++from)
        {
            const VisitData &visit = data.visit(from);
            weightScale = std::max(weightScale, visit.weight);
            volumeScale = std::max(volumeScale, visit.volume);
            timeScale = std::max(timeScale, visit.serviceTime);
            for (int to = 0; to <= static_cast<int>(data.visitCount()); ++to)
            {
                distanceScale = std::max(distanceScale, data.distance(from, to));
                timeScale = std::max(timeScale, data.duration(from, to));
            }
        }
        distanceScale = distanceScale > 0.0 ? distanceScale : 1.0;
        weightScale = weightScale > 0.0 ? weightScale : data.weightCapacity();
        volumeScale = volumeScale > 0.0 ? volumeScale : data.volumeCapacity();
        timeScale = timeScale > 0.0 ? timeScale : data.horizon();
        if (timeScale <= 0.0)
        {
            throw std::invalid_argument("time penalty scale needs a positive horizon or travel time");
        }

        const auto scaled = [&](double scale)
        { return std::clamp(distanceScale / scale, options.minPenalty, options.maxPenalty); };
        return {scaled(weightScale), scaled(volumeScale), scaled(timeScale)};
    }

    PenaltyController::PenaltyController(const Penalties &initial, const PenaltyControlOptions &options)
        : penalties_(initial), options_(options)
    {
        validatePenaltyControlOptions(options_);
        validatePenalties(penalties_);
        for (double value : {penalties_.weight, penalties_.volume, penalties_.timeWarp})
        {
            if (value < options_.minPenalty || value > options_.maxPenalty)
            {
                throw std::invalid_argument("initial penalties must lie within the control bounds");
            }
        }
    }

    bool PenaltyController::observe(const CostBreakdown &cost)
    {
        weightFeasible_ += cost.weightExcess <= kWeightTolerance;
        volumeFeasible_ += cost.volumeExcess <= kVolumeTolerance;
        timeFeasible_ += cost.timeWarp <= kTimeTolerance;
        if (++observations_ < options_.windowSize)
        {
            return false;
        }

        const auto adjust = [&](double value, std::size_t feasible)
        {
            const double rate = static_cast<double>(feasible) / static_cast<double>(observations_);
            if (rate + 1e-12 < options_.targetFeasible - options_.tolerance)
            {
                return std::min(options_.maxPenalty, value * options_.increaseFactor);
            }
            if (rate > options_.targetFeasible + options_.tolerance + 1e-12)
            {
                return std::max(options_.minPenalty, value * options_.decreaseFactor);
            }
            return value;
        };
        const Penalties next{
            adjust(penalties_.weight, weightFeasible_),
            adjust(penalties_.volume, volumeFeasible_),
            adjust(penalties_.timeWarp, timeFeasible_)};
        const bool changed = next.weight != penalties_.weight || next.volume != penalties_.volume ||
                             next.timeWarp != penalties_.timeWarp;
        penalties_ = next;
        observations_ = weightFeasible_ = volumeFeasible_ = timeFeasible_ = 0;
        updates_ += changed;
        return changed;
    }
}
