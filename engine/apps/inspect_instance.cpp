#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>

#include "router/instance_io.hpp"
#include "router/split.hpp"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: inspect_instance <path-to-_instance.json>\n";
        return 1;
    }

    try
    {
        router::Instance inst = router::loadInstance(argv[1]);

        std::cout << "Loaded: " << argv[1] << "\n";
        std::cout << "  seed            = " << inst.seed << "\n";
        std::cout << "  n (customers)   = " << inst.n << "\n";
        std::cout << "  horizon (s)     = " << inst.horizon << "\n";
        std::cout << "  fleet.size      = " << inst.fleet.size << "\n";
        std::cout << "  fleet.w_max     = " << inst.fleet.weightCapacity << "\n";
        std::cout << "  fleet.v_max     = " << inst.fleet.volumeCapacity << "\n";
        std::cout << "  nodes.size()    = " << inst.nodes.size() << "\n";
        std::cout << "  matrix dims     = " << inst.durationMatrix.size()
                  << " x " << (inst.durationMatrix.empty() ? 0 : inst.durationMatrix[0].size()) << "\n";

        const auto &depot = inst.nodes.front();
        std::cout << "  depot osm_id    = " << depot.osmId << " (type=" << depot.type << ")\n";

        if (inst.nodes.size() > 1)
        {
            const auto &c1 = inst.nodes[1];
            std::cout << "  nodes[1] osm_id = " << c1.osmId
                      << ", demand_weight=" << c1.demandWeight
                      << ", tw=[" << c1.twStart << "," << c1.twEnd << "]\n";
        }

        std::vector<router::Visit> visits = router::splitCustomers(inst);

        int splitCustomerCount = 0;
        int maxChunks = 1;
        double worstWeightDrift = 0.0;
        double worstVolumeDrift = 0.0;

        std::map<int, double> weightSum, volumeSum;
        for (const auto &v : visits)
        {
            weightSum[v.nodeIndex] += v.weight;
            volumeSum[v.nodeIndex] += v.volume;
            if (v.totalChunks > maxChunks)
                maxChunks = v.totalChunks;
        }
        for (std::size_t idx = 1; idx < inst.nodes.size(); ++idx)
        {
            const auto &node = inst.nodes[idx];
            if (weightSum.count(static_cast<int>(idx)) == 0)
                continue;
            const double wDrift = std::abs(weightSum[static_cast<int>(idx)] - node.demandWeight);
            const double vDrift = std::abs(volumeSum[static_cast<int>(idx)] - node.demandVolume);
            worstWeightDrift = std::max(worstWeightDrift, wDrift);
            worstVolumeDrift = std::max(worstVolumeDrift, vDrift);
        }
        for (std::size_t idx = 1; idx < inst.nodes.size(); ++idx)
        {
            const auto &node = inst.nodes[idx];
            const int m = std::max({1,
                                    static_cast<int>(std::ceil(node.demandWeight / inst.fleet.weightCapacity)),
                                    static_cast<int>(std::ceil(node.demandVolume / inst.fleet.volumeCapacity))});
            if (m > 1)
                splitCustomerCount++;
        }

        std::cout << "\n  --- splitCustomers() ---\n";
        std::cout << "  total visits (chunks) = " << visits.size() << "\n";
        std::cout << "  customers split (m>1) = " << splitCustomerCount
                  << " / " << inst.n << "\n";
        std::cout << "  max chunks for 1 cust  = " << maxChunks << "\n";
        std::cout << std::setprecision(10);
        std::cout << "  worst weight drift     = " << worstWeightDrift << "\n";
        std::cout << "  worst volume drift     = " << worstVolumeDrift << "\n";

        std::cout << "OK\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}