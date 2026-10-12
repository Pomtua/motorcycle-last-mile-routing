#pragma once

#include <cmath>
#include <cstdint>

namespace router::hgs
{
    inline constexpr double kUnitsPerKilogram = 100.0;

    struct Delivery
    {
        int customer = 0;
        std::int64_t units = 0;

        bool operator==(const Delivery &) const = default;
    };

    inline std::int64_t toUnits(double weight)
    {
        return std::llround(weight * kUnitsPerKilogram);
    }

    inline double toWeight(std::int64_t units)
    {
        return static_cast<double>(units) / kUnitsPerKilogram;
    }
}
