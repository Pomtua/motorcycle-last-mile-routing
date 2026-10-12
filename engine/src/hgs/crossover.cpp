#include "router/hgs/crossover.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace router::hgs
{
    namespace
    {
        void validateParents(const std::vector<int> &first, const std::vector<int> &second)
        {
            if (!isCompletePermutation(first, first.size()) || !isCompletePermutation(second, first.size()))
            {
                throw std::invalid_argument("crossover parents must be permutations of the same visits");
            }
        }
    }

    bool isCompletePermutation(const std::vector<int> &tokens, std::size_t tokenCount)
    {
        if (tokens.size() != tokenCount)
        {
            return false;
        }
        std::vector<bool> seen(tokenCount + 1, false);
        for (int token : tokens)
        {
            if (token < 1 || static_cast<std::size_t>(token) > tokenCount || seen[static_cast<std::size_t>(token)])
            {
                return false;
            }
            seen[static_cast<std::size_t>(token)] = true;
        }
        return true;
    }

    std::vector<int> tokenSequence(const ProblemData &data, const Individual &individual)
    {
        std::vector<std::vector<bool>> used(data.customerCount() + 1);
        for (int customer = 1; customer <= static_cast<int>(data.customerCount()); ++customer)
        {
            used[static_cast<std::size_t>(customer)].assign(data.tokensOf(customer).size(), false);
        }
        const auto claim = [&](const Delivery &delivery) -> int
        {
            const auto &tokens = data.tokensOf(delivery.customer);
            auto &taken = used[static_cast<std::size_t>(delivery.customer)];
            std::size_t chosen = tokens.size();
            for (std::size_t index = 0; index < tokens.size(); ++index)
            {
                if (taken[index])
                {
                    continue;
                }
                if (data.token(tokens[index]).units == delivery.units)
                {
                    chosen = index;
                    break;
                }
                chosen = chosen == tokens.size() ? index : chosen;
            }
            if (chosen == tokens.size())
            {
                return 0;
            }
            taken[chosen] = true;
            return tokens[chosen];
        };

        std::vector<std::vector<int>> slots;
        std::vector<std::size_t> lastSlotOf(data.customerCount() + 1, 0);
        std::vector<bool> seen(data.customerCount() + 1, false);
        for (const Delivery &delivery : individual.giantTour)
        {
            const int token = claim(delivery);
            if (token == 0)
            {
                continue;
            }
            slots.push_back({token});
            lastSlotOf[static_cast<std::size_t>(delivery.customer)] = slots.size() - 1;
            seen[static_cast<std::size_t>(delivery.customer)] = true;
        }
        for (int customer = 1; customer <= static_cast<int>(data.customerCount()); ++customer)
        {
            const auto index = static_cast<std::size_t>(customer);
            const auto &tokens = data.tokensOf(customer);
            for (std::size_t position = 0; position < tokens.size(); ++position)
            {
                if (used[index][position])
                {
                    continue;
                }
                if (!seen[index])
                {
                    throw std::invalid_argument("token sequence needs every customer in the giant tour");
                }
                slots[lastSlotOf[index]].push_back(tokens[position]);
            }
        }

        std::vector<int> sequence;
        sequence.reserve(data.tokenCount());
        for (const auto &slot : slots)
        {
            sequence.insert(sequence.end(), slot.begin(), slot.end());
        }
        return sequence;
    }

    std::vector<Delivery> deliveriesFromTokens(const ProblemData &data, const std::vector<int> &tokens)
    {
        if (!isCompletePermutation(tokens, data.tokenCount()))
        {
            throw std::invalid_argument("deliveries need a complete permutation of canonical tokens");
        }
        std::vector<Delivery> deliveries;
        deliveries.reserve(tokens.size());
        for (int token : tokens)
        {
            deliveries.push_back(data.token(token));
        }
        return deliveries;
    }

    std::vector<int> orderedCrossover(
        const std::vector<int> &first,
        const std::vector<int> &second,
        std::size_t begin,
        std::size_t end)
    {
        validateParents(first, second);
        const std::size_t count = first.size();
        if (count == 0)
        {
            return {};
        }
        if (begin >= end || end > count)
        {
            throw std::invalid_argument("crossover segment must be a non-empty half-open range");
        }

        std::vector<int> child(count, 0);
        std::vector<bool> used(count + 1, false);
        for (std::size_t index = begin; index < end; ++index)
        {
            child[index] = first[index];
            used[static_cast<std::size_t>(first[index])] = true;
        }

        std::size_t write = end % count;
        for (std::size_t scanned = 0, read = end % count; scanned < count; ++scanned, read = (read + 1) % count)
        {
            const int visit = second[read];
            if (used[static_cast<std::size_t>(visit)])
            {
                continue;
            }
            child[write] = visit;
            used[static_cast<std::size_t>(visit)] = true;
            write = (write + 1) % count;
        }
        return child;
    }

    std::vector<int> orderedCrossover(
        const std::vector<int> &first,
        const std::vector<int> &second,
        Rng &rng)
    {
        validateParents(first, second);
        if (first.empty())
        {
            return {};
        }
        std::uniform_int_distribution<std::size_t> draw(0, first.size() - 1);
        const std::size_t a = draw(rng);
        const std::size_t b = draw(rng);
        return orderedCrossover(first, second, std::min(a, b), std::max(a, b) + 1);
    }
}
