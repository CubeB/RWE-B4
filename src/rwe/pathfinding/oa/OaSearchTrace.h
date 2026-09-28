// Ported from OpenAnnihilation, src/sim/ground-orders/include/oa/sim/ground_orders/search_trace.hpp.
// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace rwe
{
    namespace oa
    {
        /**
         * Rebuilds a route from the finish cell back to the start through the predecessor
         * directions.
         *
         * Only cells where the direction changes are kept, in a ring of the last 64,
         * and the route is returned start first as footprint centres. Throws
         * std::invalid_argument for a truncated map, a direction past 7, a path leaving
         * the map or a cycle.
         *
         * @param width search map width in cells
         * @param height search map height in cells
         * @param start start cell
         * @param finish finish cell
         * @param footprint_x unit footprint width in cells
         * @param footprint_z unit footprint depth in cells
         * @param predecessor_direction SearchMapCell::predecessor of each cell, 0..7
         * @return route points in integer world X/Z, at most 64
         */
        std::vector<std::array<int16_t, 2>> reconstructSearchPath(
            uint32_t width,
            uint32_t height,
            std::array<int16_t, 2> start,
            std::array<int16_t, 2> finish,
            int16_t footprint_x,
            int16_t footprint_z,
            std::span<const uint8_t> predecessor_direction);
    }
}
