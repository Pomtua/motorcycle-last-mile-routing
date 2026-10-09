#include <cmath>
#include <limits>
#include <vector>

#include "hgs/test_support.hpp"
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

    void testInitialPenalties()
    {
        router::Instance inst;
        inst.n = 2;
        inst.horizon = 1000.0;
        inst.fleet = {2, 20.0, 0.1};
        inst.nodes.resize(3);
        for (auto &node : inst.nodes)
        {
            node.twEnd = 1000;
        }
        inst.nodes[1].serviceTime = 10;
        inst.nodes[2].serviceTime = 30;
        inst.distanceMatrix = {{0.0, 40.0, 100.0}, {60.0, 0.0, 20.0}, {80.0, 30.0, 0.0}};
        inst.durationMatrix = {{0.0, 5.0, 50.0}, {6.0, 0.0, 2.0}, {8.0, 3.0, 0.0}};
        const std::vector<router::Visit> catalog{{1, 0, 1, 4.0, 0.02}, {2, 0, 1, 10.0, 0.05}};
        const Penalties computed = router::hgs::initialPenalties(router::hgs::ProblemData(inst, catalog));
        expect(hgs_test::near(computed.weight, 100.0 / 10.0) &&
                   hgs_test::near(computed.volume, 100.0 / 0.05) &&
                   hgs_test::near(computed.timeWarp, 100.0 / 50.0),
               "initial penalties must scale the longest distance by the largest chunk load and longest time");

        const router::Instance randomInst = hgs_test::makeRandomInstance(505, {.customers = 10});
        const router::hgs::ProblemData data(randomInst, router::splitCustomers(randomInst));
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
    testInitialPenalties();
    testWindowAndDirection();
    testBounds();
    testValidation();
    return hgs_test::finish("hgs penalty_controller");
}
