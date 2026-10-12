#include "router/split.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace router
{

    namespace
    {
        struct Chunks
        {
            int m = 1;
            std::vector<double> weight;
            std::vector<double> volume;
        };
        Chunks splitChunks(double w, double v, double wMax, double vMax)
        {
            const long long wScaled = std::llround(w * 100.0);
            const long long vScaled = std::llround(v * 10000.0);
            const long long wCap = static_cast<long long>(std::floor(wMax * 100.0));
            const long long vCap = static_cast<long long>(std::floor(vMax * 10000.0));
            if (wScaled == 0)
            {
                if (v > 0.0)
                {
                    throw std::invalid_argument("a customer with volume demand must also have weight demand");
                }
                return {1, {0.0}, {0.0}};
            }
            const double volumePerUnit = v / static_cast<double>(wScaled);

            const long long wFull = wCap * vScaled <= vCap * wScaled ? wCap : (wScaled * vCap) / vScaled;
            if (wFull <= 0)
            {
                throw std::invalid_argument("a customer's volume per kilogram leaves no room for 0.01 kg in one vehicle");
            }
            const long long m = std::max(1LL, (wScaled + wFull - 1) / wFull);

            Chunks out;
            out.m = static_cast<int>(m);
            out.weight.reserve(static_cast<std::size_t>(m));
            out.volume.reserve(static_cast<std::size_t>(m));
            for (long long i = 0; i + 1 < m; ++i)
            {
                out.weight.push_back(static_cast<double>(wFull) / 100.0);
                out.volume.push_back(static_cast<double>(wFull) * volumePerUnit);
            }
            const long long wLast = wScaled - wFull * (m - 1);
            out.weight.push_back(static_cast<double>(wLast) / 100.0);
            out.volume.push_back(m == 1 ? v : static_cast<double>(wLast) * volumePerUnit);
            return out;
        }

    }

    std::vector<Visit> splitCustomers(const Instance &inst)
    {
        std::vector<Visit> visits;
        visits.reserve(inst.nodes.size());

        const double wMax = inst.fleet.weightCapacity;
        const double vMax = inst.fleet.volumeCapacity;

        for (int idx = 1; idx < static_cast<int>(inst.nodes.size()); ++idx)
        {
            const Node &node = inst.nodes[idx];

            const Chunks ch = splitChunks(node.demandWeight, node.demandVolume, wMax, vMax);

            for (int c = 0; c < ch.m; ++c)
            {
                Visit v;
                v.nodeIndex = idx;
                v.chunkIdx = c;
                v.totalChunks = ch.m;
                v.weight = ch.weight[static_cast<std::size_t>(c)];
                v.volume = ch.volume[static_cast<std::size_t>(c)];
                visits.push_back(v);
            }
        }

        return visits;
    }

}