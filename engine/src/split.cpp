#include "router/split.hpp"

#include <algorithm>
#include <cmath>

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

            int m = std::max({1,
                              static_cast<int>(std::ceil(w / wMax)),
                              static_cast<int>(std::ceil(v / vMax))});

            std::vector<long long> wChunks;
            std::vector<long long> vChunks;

            while (true)
            {
                wChunks.clear();
                vChunks.clear();

                if (m == 1)
                {
                    wChunks.push_back(wScaled);
                    vChunks.push_back(vScaled);
                }
                else
                {
                    long long wFull = 0;
                    long long vFull = 0;

                    if (wCap * vScaled <= vCap * wScaled)
                    {
                        wFull = wCap;
                        vFull = (vScaled * wCap) / wScaled;
                    }
                    else
                    {
                        vFull = vCap;
                        wFull = (wScaled * vCap) / vScaled;
                    }

                    wChunks.assign(static_cast<std::size_t>(m - 1), wFull);
                    vChunks.assign(static_cast<std::size_t>(m - 1), vFull);
                    wChunks.push_back(wScaled - wFull * (m - 1));
                    vChunks.push_back(vScaled - vFull * (m - 1));
                }

                bool fits = true;
                for (std::size_t i = 0; i < wChunks.size(); ++i)
                {
                    if (wChunks[i] > wCap || vChunks[i] > vCap)
                    {
                        fits = false;
                        break;
                    }
                }
                if (fits)
                {
                    break;
                }
                ++m;
            }

            Chunks out;
            out.m = m;
            out.weight.reserve(wChunks.size());
            out.volume.reserve(vChunks.size());
            for (std::size_t i = 0; i < wChunks.size(); ++i)
            {
                out.weight.push_back(static_cast<double>(wChunks[i]) / 100.0);
                out.volume.push_back(static_cast<double>(vChunks[i]) / 10000.0);
            }
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