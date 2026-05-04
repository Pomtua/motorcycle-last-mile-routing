#pragma once
#include "Solver.h"
#include "RoutingInstance.h"
#include "ResultStructures.h"

namespace routing {

class ALNSSolver : public Solver {
public:
    RoutingResult solve(const RoutingInstance& instance) override;

private:
    double calculateCost(const RoutingInstance& instance, const RoutingResult& result);
};

}
