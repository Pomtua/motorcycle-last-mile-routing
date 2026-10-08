#include <chrono>

#include "hgs/test_support.hpp"
#include "router/hgs/search_context.hpp"

using hgs_test::expect;
using router::hgs::Deadline;
using router::hgs::SteadyClock;

namespace
{
    struct FakeClock
    {
        SteadyClock::time_point now = SteadyClock::time_point{} + std::chrono::seconds(100);

        Deadline::ClockFunction function()
        {
            return [this]() { return now; };
        }
    };

    void testUnsetDeadline()
    {
        Deadline deadline;
        bool neverExpires = !deadline.isSet();
        for (int call = 0; call < 10; ++call)
        {
            neverExpires = neverExpires && !deadline.expired() && !deadline.expiredEvery(1);
        }
        expect(neverExpires && deadline.clockReads() == 0, "an unset deadline must never expire or read the clock");
        expect(!deadline.withinFractionOfRemaining(0.5).isSet(), "a share of no deadline must stay unset");
    }

    void testAmortizedChecks()
    {
        FakeClock clock;
        Deadline deadline(clock.now + std::chrono::seconds(10), clock.function());

        bool quietBeforeInterval = true;
        for (int call = 0; call < 3; ++call)
        {
            quietBeforeInterval = quietBeforeInterval && !deadline.expiredEvery(4);
        }
        expect(quietBeforeInterval && deadline.clockReads() == 0, "amortized checks must skip the clock until the interval");
        expect(!deadline.expiredEvery(4) && deadline.clockReads() == 1, "the interval call must read the clock once");

        clock.now += std::chrono::seconds(10);
        bool lateUntilRead = true;
        for (int call = 0; call < 3; ++call)
        {
            lateUntilRead = lateUntilRead && !deadline.expiredEvery(4);
        }
        expect(lateUntilRead, "amortized checks may notice expiry only at the next read");
        expect(deadline.expiredEvery(4) && deadline.clockReads() == 2, "the next read must observe expiry");

        clock.now -= std::chrono::seconds(5);
        expect(deadline.expired() && deadline.expiredEvery(1000) && deadline.clockReads() == 2,
               "expiry must be sticky and stop reading the clock");
    }

    void testFractionOfRemaining()
    {
        FakeClock clock;
        Deadline deadline(clock.now + std::chrono::seconds(10), clock.function());
        Deadline half = deadline.withinFractionOfRemaining(0.5);

        clock.now += std::chrono::milliseconds(4999);
        expect(!half.expired() && !deadline.expired(), "half the budget must not expire early");
        clock.now += std::chrono::milliseconds(1);
        expect(half.expired() && !deadline.expired(), "half the budget must expire at its share of the remaining time");

        hgs_test::expectInvalidArgument([&]() { deadline.withinFractionOfRemaining(0.0); }, "zero fractions must be rejected");
        hgs_test::expectInvalidArgument([&]() { deadline.withinFractionOfRemaining(1.5); }, "fractions above one must be rejected");
        hgs_test::expectInvalidArgument([&]() { Deadline bad(clock.now, nullptr); }, "deadlines need a callable clock");
    }

    void testScopedTimer()
    {
        router::hgs::StageStats stats;
        {
            router::hgs::ScopedTimer first(stats);
        }
        {
            router::hgs::ScopedTimer second(stats);
        }
        expect(stats.calls == 2 && stats.totalMs >= 0.0, "scoped timers must record one call per scope");
    }
}

int main()
{
    testUnsetDeadline();
    testAmortizedChecks();
    testFractionOfRemaining();
    testScopedTimer();
    return hgs_test::finish("hgs search_context");
}
