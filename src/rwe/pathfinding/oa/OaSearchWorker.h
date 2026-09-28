// Ported from OpenAnnihilation, src/sim/ground-orders/include/oa/sim/ground_orders/search_worker.hpp.
// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OaSearchGoal.h"
#include "OaSearchHeap.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace rwe
{
    namespace oa
    {
        // Step direction tables. Index is the predecessor-direction byte.
        inline constexpr std::array<int8_t, 8> search_direction_x{0, -1, -1, -1, 0, 1, 1, 1};
        inline constexpr std::array<int8_t, 8> search_direction_z{-1, -1, 0, 1, 1, 1, 0, -1};

        // The path search's turn penalties, indexed by relative turn & 7.
        inline constexpr std::array<uint8_t, 8> search_turn_penalty{0, 40, 60, 80, 100, 80, 60, 40};
        inline constexpr uint8_t search_cardinal_cost = 0x10; // even (cardinal) directions
        inline constexpr uint8_t search_diagonal_cost = 0x16; // odd (diagonal) directions
        inline constexpr int32_t search_early_turn_penalty = 0x4b;
        inline constexpr int16_t search_early_run_limit = 5;
        inline constexpr int16_t search_difficult_extra = 0x1e;
        inline constexpr int16_t search_start_run = 100;
        inline constexpr int32_t search_slice_expansions = 100;     // job scan inner budget
        inline constexpr int32_t search_initial_fan = 4;            // fan when a job starts
        inline constexpr int32_t search_continue_fan = 2;           // fan after the first pop
        inline constexpr int32_t search_heuristic_scale = 0x18000;  // initial base heuristic weight
        inline constexpr uint32_t search_goal_contact_flag = 0x100; // order event: start inside goal
        // Order event: the start is off the grid, or the wall-follow seed missed the goal.
        inline constexpr uint32_t search_seed_unresolved_flag = 0x200;
        inline constexpr uint8_t search_goal_bit = 4;
        inline constexpr uint8_t search_seed_bit = 8;
        inline constexpr uint16_t search_heading_bias = 0x1000;
        inline constexpr uint32_t search_heading_shift = 13;

        enum class SearchCellVisit : uint8_t
        {
            unvisited = 0,
            open = 1,
            closed = 2,
            blocked = 3
        };

        enum class SearchAdvance : uint8_t
        {
            in_progress,
            succeeded,
            failed
        };

        struct SearchMapCell
        {
            uint8_t flags{};
            uint8_t predecessor{};
            uint16_t handle{};
        };

        static_assert(sizeof(SearchMapCell) == 4);

        /** A route point in integer world X/Z. */
        using RoutePoint = std::array<int16_t, 2>;

        /**
         * The path search's cell grid. count is (width * height + 7) & ~7, the
         * rounded cell count, not the raw product.
         */
        struct SearchCellGrid
        {
            std::vector<SearchMapCell> cells;
            int32_t width{};
            int32_t height{};
            uint32_t count{};

            /**
             * Drops the previous cells and allocates the rounded count; none when it is zero.
             *
             * @param map_width map width in cells
             * @param map_height map height in cells
             */
            void allocate(int32_t map_width, int32_t map_height);
        };

        /** The searching unit's geometry, read directly by the search. */
        struct SearchUnit
        {
            int16_t cellX{};
            int16_t cellZ{};
            int16_t footprintX{};
            int16_t footprintZ{};
            uint16_t heading{};
        };

        /**
         * The unit and goal a search job runs against.
         *
         * The movement map the original searches is supplied as `SearchBegin`'s
         * classify callback instead, so this layer owes nothing to RWE's
         * occupancy or terrain types.
         */
        struct SearchRecord
        {
            const SearchUnit* unit{};
            const SearchGoal* goal{};
        };

        // Job inputs that are not on the unit record (cell, footprint and heading are
        // read from the record itself).
        struct SearchBegin
        {
            /** The search grid's dimensions in cells. */
            int32_t mapWidth{};
            int32_t mapHeight{};
            int32_t heuristicScale{search_heuristic_scale};
            /**
             * Classifies a cell the way the original's movement map does: 0
             * blocked, 1 tight (a step through it costs the difficult extra),
             * anything higher clear. Called at most once per cell per visit.
             */
            using ClassifyFn = uint32_t (*)(void* context, int32_t x, int32_t z);
            ClassifyFn classify{};
            void* classifyContext{};
        };

        /**
         * Tests whether a wall-follow cell crossed the axis-aligned path from the start to the
         * nearest goal cell.
         *
         * Coordinates are mirrored so the goal lies at nonnegative offsets; the cell
         * crosses on the start row between the start and the goal column, or on the
         * goal column between the start and the goal row.
         *
         * @param start_x wall-follow start column
         * @param start_z wall-follow start row
         * @param goal_x nearest goal column
         * @param goal_z nearest goal row
         * @param cell_x tested cell column
         * @param cell_z tested cell row
         * @return true when the cell lies on that path
         */
        [[nodiscard]] bool crossed_start_goal_axis(
            int32_t start_x,
            int32_t start_z,
            int32_t goal_x,
            int32_t goal_z,
            int32_t cell_x,
            int32_t cell_z) noexcept;

        /**
         * Divides the tick credit by the live player count (Game.player_count).
         *
         * @param tick_credit search budget per tick
         * @param player_count live players
         * @return each player's share, 0 without players
         */
        [[nodiscard]] int32_t search_player_credit(int32_t tick_credit, uint16_t player_count) noexcept;

        /**
         * Returns a player's heuristic scale after the refresh counter wraps at 150.
         *
         * Players whose units searched rarely get a greedier (larger) heuristic: six
         * times the base below one pick per unit, three times below two. Throws
         * std::domain_error for zero units per player.
         *
         * @param usage_count jobs the player's units started since the last refresh
         * @param units_per_player unit slots per player
         * @param base base heuristic scale (16.16)
         * @return the scale
         */
        [[nodiscard]] int32_t
        search_rescaled_heuristic(int32_t usage_count, uint16_t units_per_player, int32_t base);

        /**
         * Asynchronous expander that produces routes for one job at a time. Job
         * selection and slicing belong to the caller.
         */
        class SearchWorker
        {
        public:
            /**
             * Starts a job.
             *
             * Marks goal cells, seeds the wall-follow threshold and pushes the
             * start node.
             *
             * @param record unit and goal; both must be set
             * @param begin grid size, heuristic scale and classify callback
             * @return in_progress, or failed when the job ends at once (start inside the goal, off the map, or unreachable)
             */
            [[nodiscard]] SearchAdvance
            begin(SearchRecord record, const SearchBegin& begin);

            /**
             * Pops the best open node and expands its neighbours within the fan.
             *
             * @return 1 when the node is a goal cell (the finish is recorded), else 0
             */
            [[nodiscard]] uint32_t expand_best();

            /**
             * Runs up to 100 expansions of an isolated job; the scheduler slices itself.
             *
             * @return succeeded with the route published, failed when the open set is exhausted, else in_progress
             */
            [[nodiscard]] SearchAdvance advance_slice();

            /**
             * Returns a cell's movement-map class after the sight test.
             *
             * @param x cell column
             * @param z cell row
             * @return 0 off the map, 2 when the job's player has not seen the cell, else the movement-map class
             */
            [[nodiscard]] uint32_t classify(int32_t x, int32_t z) const;

            /**
             * Returns the goal cost scaled by the job's heuristic weight.
             *
             * @param x cell column
             * @param z cell row
             * @return goal_cost * scale >> 16
             */
            [[nodiscard]] int32_t heuristic(int32_t x, int32_t z) const;

            /** Returns whether no unpopped open node remains. */
            [[nodiscard]] bool exhausted() const noexcept { return heap_.exhausted(); }

            /** Rebuilds the route from the finish cell and stores it in route(). */
            void publish_route();

            /** Returns the last published route; empty after a failure. */
            [[nodiscard]] const std::vector<RoutePoint>& route() const noexcept { return route_; }

            /** Returns the order events the job raised. */
            [[nodiscard]] uint32_t order_flags() const noexcept { return order_flags_; }

            /** Returns the relative turns expanded either side of straight ahead. */
            [[nodiscard]] int32_t fan() const noexcept { return fan_; }

            /** Sets the relative turns expanded either side of straight ahead. */
            void set_fan(int32_t fan) noexcept { fan_ = fan; }

            /** Returns the wall-follow cost threshold for goal-bit marking. */
            [[nodiscard]] int32_t threshold() const noexcept { return threshold_; }

            /** Returns the expansions or wall-follow steps spent so far. */
            [[nodiscard]] int32_t budget() const noexcept { return budget_; }

            /** Returns the job's start cell. */
            [[nodiscard]] std::array<int16_t, 2> start_cell() const noexcept { return start_; }

            /** Returns the goal cell the job reached. */
            [[nodiscard]] std::array<int16_t, 2> finish_cell() const noexcept { return finish_; }

            /** Returns the goal cell nearest the start. */
            [[nodiscard]] std::array<int32_t, 2> nearest_goal() const noexcept { return nearest_; }

            /** Returns the open-set heap. */
            [[nodiscard]] const SearchHeap& heap() const noexcept { return heap_; }

            /**
             * Returns a search-map cell.
             *
             * Throws std::out_of_range outside the allocated grid.
             *
             * @param x cell column
             * @param z cell row
             * @return the cell
             */
            [[nodiscard]] SearchMapCell cell(uint32_t x, uint32_t z) const;

            /** Returns the search map width in cells. */
            [[nodiscard]] uint32_t width() const noexcept { return width_; }

            /** Returns the search map height in cells. */
            [[nodiscard]] uint32_t height() const noexcept { return height_; }

        private:
            /** Installs the turn penalty and the cardinal/diagonal move costs. */
            void install_turn_tables();

            /**
             * Marks the eight-cell group of a cell as touched.
             *
             * @param index row-major cell index
             */
            void mark_dirty(uint32_t index);

            /** Clears the flags of every touched cell group and the touched bits. */
            void clear_dirty();

            /**
             * Marks a cell as a goal cell; cells off the map are ignored.
             *
             * @param x cell column
             * @param z cell row
             */
            void mark_goal(int32_t x, int32_t z);

            /**
             * Opens or improves one neighbour of an expanded node.
             *
             * An unvisited cell is closed as blocked when it classifies below 1 (unless on
             * the wall-follow seed), otherwise opened with its move cost, turn penalty,
             * difficult-terrain extra and early-turn penalty, and marked as a goal cell
             * when its heuristic is within the threshold. An open cell whose new cost is
             * lower takes the new predecessor and moves up the heap.
             *
             * @param parent the expanded node's payload
             * @param parent_direction the direction the search reached the parent from
             * @param relative_turn turn from that direction, 0..7
             */
            void expand_neighbor(
                const SearchHeap::Payload& parent, uint8_t parent_direction, uint32_t relative_turn);

            /**
             * Walks from the start toward the nearest goal, following walls both ways, and marks
             * the path as seed cells.
             *
             * @return the lowest heuristic seen on the walk, 0 when it reached a goal cell
             */
            int32_t seed_wall_follow();

            /** Clears the route for a failed job. */
            void fail_empty();

            /**
             * Returns a search-map cell without a bounds check.
             *
             * @param x cell column
             * @param z cell row
             * @return the cell
             */
            [[nodiscard]] SearchMapCell& at(uint32_t x, uint32_t z);

            /**
             * Returns a search-map cell without a bounds check.
             *
             * @param x cell column
             * @param z cell row
             * @return the cell
             */
            [[nodiscard]] const SearchMapCell& at(uint32_t x, uint32_t z) const;

            /**
             * Tests whether a cell lies on the search map.
             *
             * @param x cell column
             * @param z cell row
             * @return true inside the map
             */
            [[nodiscard]] bool in_map(uint32_t x, uint32_t z) const noexcept;

            SearchRecord record_{};
            SearchHeap heap_;
            std::vector<SearchMapCell> cells_;
            std::vector<uint32_t> dirty_;
            std::vector<RoutePoint> route_;
            std::array<uint8_t, 8> turn_penalty_{};
            std::array<uint8_t, 8> move_cost_{};
            std::array<int16_t, 2> start_{};
            std::array<int16_t, 2> finish_{};
            std::array<int32_t, 2> nearest_{};
            uint32_t width_{}, height_{};
            SearchBegin::ClassifyFn classify_{};
            void* classifyContext_{};
            int32_t threshold_{};
            int32_t fan_{};
            int32_t budget_{};
            int32_t heuristic_scale_{search_heuristic_scale};
            uint32_t order_flags_{};
        };
    }
}
