#pragma once
#include "RoutingInstance.h"
#include "ResultStructures.h"
#include <chrono>

namespace routing {

class Solver {
public:
    static constexpr double PENALTY_PER_UNDELIVERED = 10000000.0; 
    virtual ~Solver() = default;

    virtual RoutingResult solve(const RoutingInstance& instance) = 0;

protected:
    RoutingResult createResult(const std::string& name) {
        RoutingResult res;
        res.solver_name = name;
        return res;
    }
};

}
