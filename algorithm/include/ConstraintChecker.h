#pragma once
#include "RoutingInstance.h"
#include "ResultStructures.h"

namespace routing {

class ConstraintChecker {
public:
    static bool evaluateRoute(const RoutingInstance& instance, Route& route, const Vehicle& vehicle);

    static bool checkInsertionFeasibility(const RoutingInstance& instance, 
                                        const Route& route, 
                                        const Vehicle& vehicle, 
                                        const Parcel& parcel, 
                                        int pos, 
                                        double& cost_increase);
};

}
