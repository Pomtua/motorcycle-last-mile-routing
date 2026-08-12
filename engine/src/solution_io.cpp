#include "router/solution_io.hpp"

#include <fstream>
#include <map>
#include <stdexcept>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "router/split.hpp"

namespace router
{

    Solution loadSolution(const std::string &path, const Instance &inst)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("loadSolution: could not open file: " + path);
        }

        nlohmann::json j;
        try
        {
            file >> j;
        }
        catch (const nlohmann::json::parse_error &e)
        {
            throw std::runtime_error("loadSolution: invalid JSON in " + path + ": " + e.what());
        }

        std::unordered_map<std::string, int> osmIdToNodeIndex;
        osmIdToNodeIndex.reserve(inst.nodes.size());
        for (int idx = 0; idx < static_cast<int>(inst.nodes.size()); ++idx)
        {
            osmIdToNodeIndex[inst.nodes[static_cast<std::size_t>(idx)].osmId] = idx;
        }

        std::map<std::pair<int, int>, Visit> visitLookup;
        for (const Visit &v : splitCustomers(inst))
        {
            visitLookup[{v.nodeIndex, v.chunkIdx}] = v;
        }

        Solution solution;
        const auto &routesJson = j.at("routes");
        solution.routes.reserve(routesJson.size());

        constexpr double kWeightTolerance = 1e-6;

        for (const auto &routeJson : routesJson)
        {
            Route route;
            route.stops.reserve(routeJson.size());

            for (const auto &stopJson : routeJson)
            {
                const std::string osmId = stopJson.at("osm_id").get<std::string>();
                const int chunkIdx = stopJson.at("chunk_idx").get<int>();
                const double fileWeight = stopJson.at("weight").get<double>();

                const auto idIt = osmIdToNodeIndex.find(osmId);
                if (idIt == osmIdToNodeIndex.end())
                {
                    throw std::runtime_error(
                        "loadSolution: osm_id '" + osmId + "' in " + path +
                        " does not exist in the instance");
                }
                const int nodeIndex = idIt->second;

                const auto visitIt = visitLookup.find({nodeIndex, chunkIdx});
                if (visitIt == visitLookup.end())
                {
                    throw std::runtime_error(
                        "loadSolution: (osm_id=" + osmId + ", chunk_idx=" + std::to_string(chunkIdx) +
                        ") in " + path + " does not match any chunk our splitCustomers() produced");
                }

                const Visit &visit = visitIt->second;
                if (std::abs(visit.weight - fileWeight) > kWeightTolerance)
                {
                    throw std::runtime_error(
                        "loadSolution: weight mismatch for osm_id=" + osmId +
                        " chunk_idx=" + std::to_string(chunkIdx) +
                        " — file says " + std::to_string(fileWeight) +
                        ", our splitCustomers() says " + std::to_string(visit.weight));
                }

                route.stops.push_back(visit);
            }

            solution.routes.push_back(std::move(route));
        }

        return solution;
    }

}