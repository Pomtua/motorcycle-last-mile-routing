#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace router
{
    struct Node
    {
        std::string osmId;
        std::string type;

        double lng = 0.0;
        double lat = 0.0;

        double demandWeight = 0.0;
        double demandVolume = 0.0;

        int serviceTime = 0;
        int twStart = 0;
        int twEnd = 0;
    };

    struct Fleet
    {
        int size = 0;
        double weightCapacity = 0.0;
        double volumeCapacity = 0.0;
    };

    struct Instance
    {
        int seed = 0;
        int n = 0;
        double horizon = 0.0;

        Fleet fleet;

        std::vector<Node> nodes;

        std::vector<std::vector<double>> durationMatrix;
        std::vector<std::vector<double>> distanceMatrix;
    };

}