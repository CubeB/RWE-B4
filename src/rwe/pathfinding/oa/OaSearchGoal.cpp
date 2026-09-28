// Ported from OpenAnnihilation, src/sim/ground-orders/src/goals.cpp.
// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "OaSearchGoal.h"

#include <bit>

namespace rwe
{
    namespace oa
    {
        namespace
        {
            int32_t wrap(uint32_t v) noexcept
            {
                return std::bit_cast<int32_t>(v);
            }

            int32_t wrap_add(int32_t a, int32_t b) noexcept
            {
                return wrap(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
            }

            int32_t wrap_sub(int32_t a, int32_t b) noexcept
            {
                return wrap(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
            }

            int32_t wrap_mul(int32_t a, int32_t b) noexcept
            {
                return wrap(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
            }

            // abs(): INT_MIN stays INT_MIN.
            int32_t abs32(int32_t v) noexcept
            {
                return v < 0 ? wrap_sub(0, v) : v;
            }

            // Octile distance with the long axis weighted 18 and the short axis 7.
            int32_t octile(const SearchGoal& g, int32_t x, int32_t z) noexcept
            {
                const auto dx = abs32(wrap_sub(x, g.cellX));
                const auto dz = abs32(wrap_sub(z, g.cellZ));
                if (dz < dx)
                {
                    return wrap_add(wrap_mul(dz, 7), wrap_mul(dx, 0x12));
                }
                return wrap_add(wrap_mul(dx, 7), wrap_mul(dz, 0x12));
            }

            int32_t squared_offset(const SearchGoal& g, int32_t x, int32_t z) noexcept
            {
                const auto dx = wrap_sub(x, g.cellX);
                const auto dz = wrap_sub(z, g.cellZ);
                return wrap_add(wrap_mul(dx, dx), wrap_mul(dz, dz));
            }
        }

        bool goalContains(const SearchGoal& goal, int32_t cellX, int32_t cellZ) noexcept
        {
            return squared_offset(goal, cellX, cellZ) <= goal.radiusSquared;
        }

        int32_t goalCost(const SearchGoal& goal, int32_t cellX, int32_t cellZ) noexcept
        {
            const auto d = octile(goal, cellX, cellZ);
            return d < goal.tolerance ? 0 : wrap_sub(d, goal.tolerance);
        }
    }
}
