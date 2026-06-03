#pragma once
#include "Solver.h"

namespace routing {

class ORToolsSolver : public Solver {
public:
    RoutingResult solve(const RoutingInstance& instance) override {
        return solve(instance, 1.0);
    }
    RoutingResult solve(const RoutingInstance& instance, double time_limit_seconds);
};

}
