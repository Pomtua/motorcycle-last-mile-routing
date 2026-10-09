#include <cmath>
#include <limits>
#include <vector>

#include "hgs/test_support.hpp"
#include "router/hgs.hpp"
#include "router/hgs/cost_model.hpp"
#include "router/hgs/penalty_controller.hpp"
#include "router/hgs/problem_data.hpp"
#include "router/split.hpp"

using hgs_test::expect;
using hgs_test::expectInvalidArgument;
using router::hgs::CostBreakdown;
using router::hgs::PenaltyControlOptions;
using router::hgs::PenaltyController;
using router::hgs::Penalties;

namespace
{
    CostBreakdown violations(double weight, double volume, double timeWarp)
    {
        CostBreakdown cost;
        cost.weightExcess = weight;
        cost.volumeExcess = volume;
        cost.timeWarp = timeWarp;
        return cost;
    }

    void testInitialPenaltiesMatchV1()
    {
        bool matches = true;
        for (std::uint32_t seed = 500; seed < 505; ++seed)
        {
            const router::Instance inst = hgs_test::makeRandomInstance(seed, {.customers = 40});
            const auto catalog = router::splitCustomers(inst);
            const router::hgs::ProblemData data(inst, catalog);
            const Penalties v2 = router::hgs::initialPenalties(data);
            const router::HgsPenaltyWeights v1 = router::makeInitialHgsPenalties(inst, catalog);
            matches = matches && hgs_test::near(v2.weight, v1.weightPenalty) &&
                      hgs_test::near(v2.volume, v1.volumePenalty) &&
                      hgs_test::near(v2.timeWarp, v1.timeWarpPenalty);
        }
        expect(matches, "initial penalties must match v1 scaling of distance against load and time");

        const router::Instance inst = hgs_test::makeRandomInstance(505, {.customers = 10});
        const router::hgs::ProblemData data(inst, router::splitCustomers(inst));
        PenaltyControlOptions narrow;
        narrow.minPenalty = 2.0;
        narrow.maxPenalty = 4.0;
        const Penalties clamped = router::hgs::initialPenalties(data, narrow);
        const auto inside = [](double value) { return value >= 2.0 && value <= 4.0; };
        expect(inside(clamped.weight) && inside(clamped.volume) && inside(clamped.timeWarp),
               "initial penalties must respect the control bounds");
    }

    void testWindowAndDirection()
    {
        PenaltyControlOptions options;
        options.windowSize = 10;
        PenaltyController controller({100.0, 100.0, 100.0}, options);

        bool quiet = true;
        for (int observation = 0; observation < 9; ++observation)
        {
            quiet = quiet && !controller.observe(violations(1.0, 0.0, 0.0));
        }
        expect(quiet && controller.penalties().weight == 100.0, "penalties must hold until the window is full");
        expect(controller.observe(violations(1.0, 0.0, 0.0)), "a full window must report a change");
        expect(hgs_test::near(controller.penalties().weight, 120.0) &&
                   hgs_test::near(controller.penalties().volume, 85.0) &&
                   hgs_test::near(controller.penalties().timeWarp, 85.0),
               "each penalty must rise when rarely satisfied and fall when always satisfied");

        PenaltyController balanced({50.0, 50.0, 50.0}, options);
        for (int observation = 0; observation < 10; ++observation)
        {
            const bool violated = observation >= 2;
            balanced.observe(violations(violated ? 1.0 : 0.0, violated ? 1.0 : 0.0, violated ? 1.0 : 0.0));
        }
        expect(balanced.penalties().weight == 50.0 && balanced.updates() == 0,
               "a feasible rate at the target must leave penalties unchanged");
    }

    void testBounds()
    {
        PenaltyControlOptions options;
        options.windowSize = 1;
        options.minPenalty = 1.0;
        options.maxPenalty = 110.0;
        PenaltyController controller({100.0, 1.1, 50.0}, options);
        controller.observe(violations(1.0, 0.0, 1.0));
        controller.observe(violations(1.0, 0.0, 1.0));
        expect(controller.penalties().weight == 110.0 && controller.penalties().volume == 1.0,
               "penalties must saturate at the control bounds");
        expect(controller.updates() == 2, "every changed window must be counted");
    }

    void testValidation()
    {
        const auto rejects = [](auto mutate, const char *message)
        {
            PenaltyControlOptions options;
            mutate(options);
            expectInvalidArgument([&]() { PenaltyController bad({1.0, 1.0, 1.0}, options); }, message);
        };
        rejects([](auto &o) { o.windowSize = 0; }, "an empty window must be rejected");
        rejects([](auto &o) { o.targetFeasible = 0.0; }, "a zero target must be rejected");
        rejects([](auto &o) { o.increaseFactor = 1.0; }, "a non-increasing factor must be rejected");
        rejects([](auto &o) { o.decreaseFactor = 1.0; }, "a non-decreasing factor must be rejected");
        rejects([](auto &o) { o.minPenalty = 0.0; }, "a zero minimum penalty must be rejected");
        rejects([](auto &o) { o.maxPenalty = 1e-9; }, "a maximum below the minimum must be rejected");
        rejects([](auto &o) { o.tolerance = std::numeric_limits<double>::quiet_NaN(); }, "NaN options must be rejected");
        expectInvalidArgument([]() { PenaltyController bad({1e13, 1.0, 1.0}, {}); },
                              "initial penalties outside the bounds must be rejected");
    }
}

int main()
{
    testInitialPenaltiesMatchV1();
    testWindowAndDirection();
    testBounds();
    testValidation();
    return hgs_test::finish("hgs penalty_controller");
}
