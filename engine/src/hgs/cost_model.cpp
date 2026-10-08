#include "router/hgs/cost_model.hpp"

#include <cmath>
#include <stdexcept>

namespace router::hgs
{
    void validatePenalties(const Penalties &penalties)
    {
        for (double value : {penalties.weight, penalties.volume, penalties.timeWarp})
        {
            if (!std::isfinite(value) || value < 0.0)
            {
                throw std::invalid_argument("penalties must be finite and non-negative");
            }
        }
    }
}
