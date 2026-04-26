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
        // Per-mille speed multipliers: 0.1x, 0.2x, 0.3x, 0.5x, 0.7x,
        // 1.0x, 1.5x, 2.0x, 3.0x, 5.0x.
        static constexpr std::array<int, 10> SpeedSteps = {
            100, 200, 300, 500, 700, 1000, 1500, 2000, 3000, 5000};
        static constexpr int DefaultIndex = 5;
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
