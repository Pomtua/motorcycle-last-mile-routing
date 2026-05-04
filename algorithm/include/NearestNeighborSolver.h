#pragma once
#include "Solver.h"
#include <set>

namespace routing {

class NearestNeighborSolver : public Solver {
public:
    RoutingResult solve(const RoutingInstance& instance) override;

private:

    bool isFeasible(const RoutingInstance& instance, 
                    const Route& current_route, 
                    int next_parcel_idx,
                    double current_weight,
                    double current_volume,
                    double current_time,
                    double& arrival_time);
};

}
