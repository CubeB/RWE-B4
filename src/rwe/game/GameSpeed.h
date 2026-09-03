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
        // The original keeps speed as 1..20 and displays it as value - 10,
        // so the HUD reads -9 to +10 with 0 at normal, and the multiplier is
        // linear on the tick budget: step n is n tenths of normal speed.
        static constexpr std::array<int, 20> SpeedSteps = {
            100, 200, 300, 400, 500, 600, 700, 800, 900,
            1000,
            1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000};
        static constexpr int DefaultIndex = 9;
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
