#pragma once

#include <array>
#include <cstddef>

namespace rwe
{
    /**
     * Game speed level. TA-style discrete speed steps stored as
     * per-mille integer multipliers so all tick-rate scaling can be
     * done with integer arithmetic. Default index 5 corresponds to
     * 1.0x (perMille == 1000).
     *
     * The display offset matches TA's HUD text: a speed of "+N" or
     * "-N" relative to the default level, where "+1" is one notch
     * faster than 1.0x and "-1" is one notch slower.
     */
    class GameSpeed
    {
    public:
        // The original's speed control runs from -10 to +10 around normal,
        // so there are twenty-one steps with 1.0x in the middle: a geometric
        // ramp down to a tenth and up to ten times.
        static constexpr std::array<int, 21> SpeedSteps = {
            100, 130, 170, 220, 280, 360, 460, 590, 750, 800,
            1000,
            1300, 1700, 2200, 2800, 3600, 4600, 5900, 7500, 8700, 10000};
        static constexpr int DefaultIndex = 10;
        static constexpr int MinIndex = 0;
        static constexpr int MaxIndex = static_cast<int>(SpeedSteps.size()) - 1;

    private:
        int idx;

    public:
        constexpr GameSpeed() : idx(DefaultIndex) {}

        constexpr explicit GameSpeed(int index) : idx(clampIndex(index)) {}

        constexpr int index() const { return idx; }

        constexpr int perMille() const { return SpeedSteps[static_cast<std::size_t>(idx)]; }

        /**
         * Display offset relative to the default speed.
         * 0 at 1.0x, positive when faster, negative when slower.
         */
        constexpr int displayOffset() const { return idx - DefaultIndex; }

        constexpr bool isDefault() const { return idx == DefaultIndex; }

        constexpr GameSpeed increased() const
        {
            return GameSpeed(idx + 1);
        }

        constexpr GameSpeed decreased() const
        {
            return GameSpeed(idx - 1);
        }

        constexpr bool operator==(const GameSpeed& rhs) const { return idx == rhs.idx; }
        constexpr bool operator!=(const GameSpeed& rhs) const { return idx != rhs.idx; }

    private:
        static constexpr int clampIndex(int i)
        {
            return i < MinIndex ? MinIndex : (i > MaxIndex ? MaxIndex : i);
        }
    };
}
