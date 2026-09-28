// Ported from OpenAnnihilation, src/sim/ground-orders/src/search_trace.cpp.
// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "OaSearchTrace.h"

#include <algorithm>
#include <stdexcept>

namespace rwe
{
    namespace oa
    {
        namespace
        {
            constexpr std::size_t retained_turns = 64;
            constexpr std::array<int8_t, 8> direction_x{0, -1, -1, -1, 0, 1, 1, 1};
            constexpr std::array<int8_t, 8> direction_z{-1, -1, 0, 1, 1, 1, 0, -1};
        }

        std::vector<std::array<int16_t, 2>> reconstructSearchPath(
            uint32_t width,
            uint32_t height,
            std::array<int16_t, 2> start,
            std::array<int16_t, 2> finish,
            int16_t footprint_x,
            int16_t footprint_z,
            std::span<const uint8_t> predecessor_direction)
        {
            if (predecessor_direction.size() < std::size_t(width) * height)
            {
                throw std::invalid_argument("search predecessor map is truncated");
            }
            std::array<std::array<int16_t, 2>, retained_turns> ring{};
            ring[0] = finish;
            auto current = finish;
            uint32_t written = 1;
            auto index = [&](const std::array<int16_t, 2>& p) {
                if (p[0] < 0 || p[1] < 0 || uint32_t(p[0]) >= width || uint32_t(p[1]) >= height)
                {
                    throw std::invalid_argument("search predecessor leaves map");
                }
                return std::size_t(uint32_t(p[1])) * width + uint32_t(p[0]);
            };
            auto previous = predecessor_direction[index(current)];
            if (previous >= direction_x.size())
            {
                throw std::invalid_argument("invalid search predecessor direction");
            }
            std::size_t guard = 0;
            while (current != start)
            {
                const auto direction = predecessor_direction[index(current)];
                if (direction >= direction_x.size())
                {
                    throw std::invalid_argument("invalid search predecessor direction");
                }
                if (direction != previous)
                {
                    ring[written & 63u] = current;
                    ++written;
                    previous = direction;
                }
                current[0] = static_cast<int16_t>(int32_t(current[0]) - direction_x[direction]);
                current[1] = static_cast<int16_t>(int32_t(current[1]) - direction_z[direction]);
                if (++guard > predecessor_direction.size())
                {
                    throw std::invalid_argument("cyclic search predecessor map");
                }
            }
            ring[written & 63u] = start;
            const auto count = std::min<uint32_t>(written + 1, retained_turns);
            std::vector<std::array<int16_t, 2>> result;
            result.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto cell = ring[written & 63u];
                --written;
                result.push_back(
                    {static_cast<int16_t>((int32_t(cell[0]) * 2 + footprint_x) * 8),
                        static_cast<int16_t>((int32_t(cell[1]) * 2 + footprint_z) * 8)});
            }
            return result;
        }
    }
}
