#include <iostream>

#include "router/instance_io.hpp"

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

        std::cout << "OK\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}