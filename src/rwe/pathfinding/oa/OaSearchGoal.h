// Ported from OpenAnnihilation, src/sim/ground-orders/include/oa/sim/ground_orders/goals.hpp.
// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace rwe
{
    namespace oa
    {
        /** A circular ground movement goal. */
        struct SearchGoal
        {
            int32_t cellX{};
            int32_t cellZ{};
            int32_t tolerance{};
            int32_t radiusSquared{};
        };

        /**
         * Tests whether a footprint-origin cell satisfies the goal.
         *
         * A circle holds cells within its squared radius.
         *
         * @param goal goal to test
         * @param cellX cell column
         * @param cellZ cell row
         * @return true when the cell satisfies the goal
         */
        bool goalContains(const SearchGoal& goal, int32_t cellX, int32_t cellZ) noexcept;

        /**
         * Returns the admissible search cost from a cell to the goal.
         *
         * The circle uses the octile distance with weights 18 and 7 less the
         * tolerance.
         *
         * @param goal goal to reach
         * @param cellX cell column
         * @param cellZ cell row
         * @return the cost, zero inside the goal
         */
        int32_t goalCost(const SearchGoal& goal, int32_t cellX, int32_t cellZ) noexcept;

        /**
         * Visits the cells the path search marks as goal cells.
         *
         * A circle visits only its centre cell.
         *
         * @param goal goal whose cells are visited
         * @param fn called with (cellX, cellZ) for each goal cell
         */
        template <typename Fn>
        void forEachGoalCell(const SearchGoal& goal, Fn&& fn)
        {
            fn(goal.cellX, goal.cellZ);
        }
    }
}
