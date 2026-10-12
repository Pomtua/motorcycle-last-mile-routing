#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "router/hgs/search_routes.hpp"

namespace router::hgs
{
    inline constexpr double kImprovementEpsilon = 1e-6;
    inline constexpr std::size_t kMaxSplitRoutes = 3;

    enum class MoveScope
    {
        Pair,
        Sibling,
        Single
    };

    std::int64_t residualUnits(const ProblemData &data, const RouteSummary &route, int customer);
    std::int64_t excessUnits(const ProblemData &data, const RouteSummary &route, int customer);

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
        virtual MoveScope scope() const = 0;

        const MoveStats &stats() const { return stats_; }
        void resetStats() { stats_ = {}; }

    protected:
        MoveStats stats_;
    };

    class Relocate final : public Move
    {
    public:
        std::string_view name() const override { return "relocate"; }
        MoveScope scope() const override { return MoveScope::Pair; }
        bool tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes) override;

        static std::optional<double> evaluate(const SearchNode &u, const SearchNode &target, const SearchRoutes &routes);
        static bool conflicts(const SearchNode &u, const SearchNode &target, const SearchRoutes &routes);
        static void apply(SearchNode &u, SearchNode &target, SearchRoutes &routes);
    };

    class Swap final : public Move
    {
    public:
        std::string_view name() const override { return "swap"; }
        MoveScope scope() const override { return MoveScope::Pair; }
        bool tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes) override;

        static std::optional<double> evaluate(const SearchNode &u, const SearchNode &v, const SearchRoutes &routes);
        static bool conflicts(const SearchNode &u, const SearchNode &v, const SearchRoutes &routes);
        static void apply(SearchNode &u, SearchNode &v, SearchRoutes &routes);
    };

    class TwoOptStar final : public Move
    {
    public:
        std::string_view name() const override { return "2-opt*"; }
        MoveScope scope() const override { return MoveScope::Pair; }
        bool tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes) override;

        static std::optional<double> evaluate(const SearchNode &u, const SearchNode &cut, const SearchRoutes &routes);
        static bool conflicts(const SearchNode &u, const SearchNode &cut, const SearchRoutes &routes);
        static void apply(SearchNode &u, SearchNode &cut, SearchRoutes &routes);
    };

    class PartialRelocate final : public Move
    {
    public:
        std::string_view name() const override { return "partial-relocate"; }
        MoveScope scope() const override { return MoveScope::Pair; }
        bool tryApply(SearchNode &u, SearchNode &v, SearchRoutes &routes) override;

        static std::optional<double> evaluate(const SearchNode &u, const SearchNode &target, std::int64_t moved, const SearchRoutes &routes);
        static void apply(SearchNode &u, SearchNode &target, std::int64_t moved, SearchRoutes &routes);
    };

    class Transfer final : public Move
    {
    public:
        std::string_view name() const override { return "transfer"; }
        MoveScope scope() const override { return MoveScope::Sibling; }
        bool tryApply(SearchNode &u, SearchNode &sibling, SearchRoutes &routes) override;

        static std::optional<double> evaluate(const SearchNode &u, const SearchNode &sibling, std::int64_t moved, const SearchRoutes &routes);
        static void apply(SearchNode &u, SearchNode &sibling, std::int64_t moved, SearchRoutes &routes);
    };

    struct SplitPiece
    {
        SearchNode *target = nullptr;
        bool merge = false;
        std::int64_t units = 0;
        double delta = 0.0;
    };

    struct SplitPlan
    {
        double delta = 0.0;
        std::vector<SplitPiece> pieces;
    };

    class SplitInsertion final : public Move
    {
    public:
        std::string_view name() const override { return "split-insertion"; }
        MoveScope scope() const override { return MoveScope::Single; }
        bool tryApply(SearchNode &u, SearchNode &unused, SearchRoutes &routes) override;

        static std::optional<SplitPlan> plan(SearchNode &u, SearchRoutes &routes);
        static void apply(SearchNode &u, const SplitPlan &plan, SearchRoutes &routes);
    };

    std::vector<std::unique_ptr<Move>> makeMoves(const std::vector<std::string> &names);
}
