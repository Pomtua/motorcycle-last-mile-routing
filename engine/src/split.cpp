#include "router/split.hpp"

#include <algorithm>
#include <cmath>

namespace router
{

    namespace
    {
        std::vector<double> splitValue(double totalVal, int m, int precision)
        {
            long long scale = 1;
            for (int i = 0; i < precision; ++i)
                scale *= 10;

            const long long totalScaled = std::llround(totalVal * static_cast<double>(scale));

            const long long base = totalScaled / m;
            const long long remainder = totalScaled % m;

            std::vector<long long> chunksScaled(static_cast<std::size_t>(m), base);
            for (long long i = 0; i < remainder; ++i)
            {
                chunksScaled[static_cast<std::size_t>(i)] += 1;
            }

            std::vector<double> result(static_cast<std::size_t>(m));
            for (int i = 0; i < m; ++i)
            {
                result[static_cast<std::size_t>(i)] =
                    static_cast<double>(chunksScaled[static_cast<std::size_t>(i)]) / static_cast<double>(scale);
            }
            return result;
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

            const int m = std::max({1,
                                    static_cast<int>(std::ceil(node.demandWeight / wMax)),
                                    static_cast<int>(std::ceil(node.demandVolume / vMax))});

            const std::vector<double> wChunks = splitValue(node.demandWeight, m, 2);
            const std::vector<double> vChunks = splitValue(node.demandVolume, m, 4);

            for (int c = 0; c < m; ++c)
            {
                Visit v;
                v.nodeIndex = idx;
                v.chunkIdx = c;
                v.totalChunks = m;
                v.weight = wChunks[static_cast<std::size_t>(c)];
                v.volume = vChunks[static_cast<std::size_t>(c)];
                visits.push_back(v);
            }
        }

        return visits;
    }

}