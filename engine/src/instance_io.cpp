#include "router/instance_io.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace router
{
    Instance loadInstance(const std::string &path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("loadInstance: could not open file: " + path);
        }

        nlohmann::json j;
        try
        {
            file >> j;
        }
        catch (const nlohmann::json::parse_error &e)
        {
            throw std::runtime_error("loadInstance: invalid JSON in " + path + ": " + e.what());
        }

        Instance inst;

        const auto &meta = j.at("meta");
        inst.seed = meta.at("seed").get<int>();
        inst.n = meta.at("n").get<int>();
        inst.horizon = meta.at("horizon").get<double>();

        const auto &fleet = j.at("fleet");
        inst.fleet.size = fleet.at("size").get<int>();
        inst.fleet.weightCapacity = fleet.at("weight_capacity").get<double>();
        inst.fleet.volumeCapacity = fleet.at("volume_capacity").get<double>();

        const auto &nodesJson = j.at("nodes");
        inst.nodes.reserve(nodesJson.size());
        for (const auto &nj : nodesJson)
        {
            Node node;
            node.osmId = nj.at("osm_id").get<std::string>();
            node.type = nj.at("type").get<std::string>();
            node.lng = nj.at("lng").get<double>();
            node.lat = nj.at("lat").get<double>();
            node.demandWeight = nj.at("demand_weight").get<double>();
            node.demandVolume = nj.at("demand_volume").get<double>();
            node.twStart = nj.at("tw_start").get<int>();
            node.twEnd = nj.at("tw_end").get<int>();
            node.serviceTime = nj.at("service_time").get<int>();
            inst.nodes.push_back(std::move(node));
        }

        inst.durationMatrix = j.at("duration_matrix").get<std::vector<std::vector<double>>>();
        inst.distanceMatrix = j.at("distance_matrix").get<std::vector<std::vector<double>>>();

        const std::size_t expectedSize = static_cast<std::size_t>(inst.n) + 1;
        if (inst.nodes.size() != expectedSize)
        {
            throw std::runtime_error(
                "loadInstance: nodes.size()=" + std::to_string(inst.nodes.size()) +
                " but meta.n+1=" + std::to_string(expectedSize) + " in " + path);
        }
        if (inst.durationMatrix.size() != expectedSize || inst.distanceMatrix.size() != expectedSize)
        {
            throw std::runtime_error("loadInstance: matrix dimensions do not match nodes.size() in " + path);
        }

        return inst;
    }
}