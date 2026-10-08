#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <random>

namespace router::hgs
{
    using Rng = std::mt19937;
    using SteadyClock = std::chrono::steady_clock;

    class Deadline
    {
    public:
        using ClockFunction = std::function<SteadyClock::time_point()>;

        Deadline() = default;
        explicit Deadline(SteadyClock::time_point at, ClockFunction clock = SteadyClock::now);

        bool isSet() const { return at_.has_value(); }
        bool expired();
        bool expiredEvery(std::size_t interval);
        Deadline withinFractionOfRemaining(double fraction);
        SteadyClock::time_point now() const;
        std::size_t clockReads() const { return clockReads_; }

    private:
        std::optional<SteadyClock::time_point> at_;
        ClockFunction clock_ = SteadyClock::now;
        std::size_t callsSinceRead_ = 0;
        std::size_t clockReads_ = 0;
        bool expired_ = false;
    };

    struct StageStats
    {
        double totalMs = 0.0;
        std::size_t calls = 0;
    };

    class ScopedTimer
    {
    public:
        explicit ScopedTimer(StageStats &stats);
        ScopedTimer(const ScopedTimer &) = delete;
        ScopedTimer &operator=(const ScopedTimer &) = delete;
        ~ScopedTimer();

    private:
        StageStats &stats_;
        SteadyClock::time_point start_;
    };
}
