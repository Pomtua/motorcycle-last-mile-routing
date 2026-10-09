#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "router/hgs/search_routes.hpp"

namespace router::hgs
{
    inline constexpr double kImprovementEpsilon = 1e-6;

    struct MoveStats
    {
        std::size_t evaluated = 0;
        std::size_t improving = 0;
        std::size_t rejectedByConflict = 0;
        std::size_t applied = 0;
    };

    class Move
    {
    public:
        virtual ~Move() = default;
        virtual std::string_view name() const = 0;
        virtual bool tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes) = 0;

        const MoveStats &stats() const { return stats_; }
        void resetStats() { stats_ = {}; }

    protected:
        MoveStats stats_;
    };

    class Relocate final : public Move
    {
    public:
        std::string_view name() const override { return "relocate"; }
        bool tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes) override;

        static std::optional<double> evaluate(const SearchNode &u, const SearchNode &target, const SearchRoutes &routes);
        static bool conflicts(const SearchNode &u, const SearchNode &target, const SearchRoutes &routes);
        static void apply(SearchNode &u, SearchNode &target, SearchRoutes &routes);
    };

    class Swap final : public Move
    {
    public:
        std::string_view name() const override { return "swap"; }
        bool tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes) override;

        static std::optional<double> evaluate(const SearchNode &u, const SearchNode &v, const SearchRoutes &routes);
        static bool conflicts(const SearchNode &u, const SearchNode &v, const SearchRoutes &routes);
        static void apply(SearchNode &u, SearchNode &v, SearchRoutes &routes);
    };

    class TwoOptStar final : public Move
    {
    public:
        std::string_view name() const override { return "2-opt*"; }
        bool tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes) override;

        static std::optional<double> evaluate(const SearchNode &u, const SearchNode &cut, const SearchRoutes &routes);
        static bool conflicts(const SearchNode &u, const SearchNode &cut, const SearchRoutes &routes);
        static void apply(SearchNode &u, SearchNode &cut, SearchRoutes &routes);
    };

    std::vector<std::unique_ptr<Move>> makeMoves(const std::vector<std::string> &names);
}
