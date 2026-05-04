#pragma once
#include "Solver.h"
#include "RoutingInstance.h"
#include "ResultStructures.h"
#include <vector>

namespace routing {

struct Chromosome {
    std::vector<int> parcel_sequence;
    RoutingResult result;
    double fitness;
};

class GASolver : public Solver {
public:
    RoutingResult solve(const RoutingInstance& instance) override;

private:
    double calculateCost(const RoutingInstance& instance, const RoutingResult& result);
    RoutingResult decode(const RoutingInstance& instance, const std::vector<int>& sequence);
    Chromosome crossover(const Chromosome& p1, const Chromosome& p2, const RoutingInstance& instance);
    void mutate(Chromosome& chrom, const RoutingInstance& instance);
};

}
