#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "router/instance_io.hpp"
#include "router/zones.hpp"

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr
            << "usage: inspect_zones <instance.json> "
               "[candidate_max_zones]\n";
        return 1;
    }

    try
    {
        const router::Instance inst =
            router::loadInstance(argv[1]);

        int candidateMaxZones = 0;
        if (argc == 3)
        {
            std::size_t parsedChars = 0;
            const std::string argument = argv[2];
            candidateMaxZones =
                std::stoi(argument, &parsedChars);
            if (parsedChars != argument.size())
            {
                throw std::invalid_argument(
                    "candidate_max_zones must be an integer");
            }
        }

        const auto start =
            std::chrono::steady_clock::now();
        const router::ZoneSelection selection =
            argc == 3
                ? router::selectZones(
                      inst, candidateMaxZones)
                : router::selectZones(inst);
        const auto end =
            std::chrono::steady_clock::now();

        const double runtimeMs =
            std::chrono::duration<double, std::milli>(
                end - start).count();

        const nlohmann::json result = {
            {"schema_version", 1},
            {"instance", argv[1]},
            {"size", inst.n},
            {"seed", inst.seed},
            {"candidate_max",
             selection.candidateMaxZones},
            {"num_zones", selection.numZones},
            {"silhouette_score",
             selection.silhouetteScore},
            {"runtime_ms", runtimeMs}
        };

        std::cout << "n              = "
                  << inst.n << "\n";
        std::cout << "candidate max  = "
                  << selection.candidateMaxZones << "\n";
        std::cout << "num_zones      = "
                  << selection.numZones << "\n";
        std::cout << "silhouette     = "
                  << selection.silhouetteScore << "\n";
        std::cout << "time           = "
                  << runtimeMs << " ms\n";
        std::cout << "RESULT_JSON "
                  << result.dump() << "\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "ERROR: "
                  << error.what() << "\n";
        return 2;
    }
}
