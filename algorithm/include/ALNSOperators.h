#pragma once
#include "RoutingInstance.h"
#include "ResultStructures.h"
#include <vector>

namespace routing {

class ALNSOperators {
public:

    static std::vector<int> destroyRandom(const RoutingInstance& instance, RoutingResult& solution, int num_to_remove);
    static std::vector<int> destroyWorst(const RoutingInstance& instance, RoutingResult& solution, int num_to_remove);

    static void repairGreedy(const RoutingInstance& instance, RoutingResult& solution, const std::vector<int>& unassigned);
    static void repairRegret(const RoutingInstance& instance, RoutingResult& solution, const std::vector<int>& unassigned);
};

}
