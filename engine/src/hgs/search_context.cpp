#include "router/hgs/search_context.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace router::hgs
{
    Deadline::Deadline(SteadyClock::time_point at, ClockFunction clock)
        : at_(at), clock_(std::move(clock))
    {
        if (!clock_)
        {
            throw std::invalid_argument("deadline clock must be callable");
        }
    }

    SteadyClock::time_point Deadline::now() const
    {
        return clock_();
    }

    bool Deadline::expired()
    {
        if (!at_ || expired_)
        {
            return expired_;
        }
        ++clockReads_;
        callsSinceRead_ = 0;
        expired_ = clock_() >= *at_;
        return expired_;
    }

    bool Deadline::expiredEvery(std::size_t interval)
    {
        if (!at_ || expired_)
        {
            return expired_;
        }
        if (++callsSinceRead_ < interval)
        {
            return false;
        }
        return expired();
    }

    Deadline Deadline::withinFractionOfRemaining(double fraction)
    {
        if (!std::isfinite(fraction) || fraction <= 0.0 || fraction > 1.0)
        {
            throw std::invalid_argument("deadline fraction must be in (0, 1]");
        }
        if (!at_)
        {
            return {};
        }
        const SteadyClock::time_point current = clock_();
        ++clockReads_;
        const auto remaining = std::chrono::duration<double>(*at_ - current);
        const auto share = std::chrono::duration_cast<SteadyClock::duration>(remaining * fraction);
        return Deadline(current + share, clock_);
    }

    ScopedTimer::ScopedTimer(StageStats &stats)
        : stats_(stats), start_(SteadyClock::now())
    {
    }

    ScopedTimer::~ScopedTimer()
    {
        stats_.totalMs += std::chrono::duration<double, std::milli>(SteadyClock::now() - start_).count();
        ++stats_.calls;
    }
}
