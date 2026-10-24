#pragma once

#include <rwe/sim/GameSimulation.h>
#include <limits>

namespace rwe
{
    /** Isolate combat mechanics from fog; fog tests must use real spotters. */
    inline void revealCombatArena(GameSimulation& sim)
    {
        sim.lineOfSightMode = LineOfSightMode::Permanent;
        sim.explored.fill([](const auto&) { return std::numeric_limits<ExploredMask>::max(); });
        for (auto& visibility : sim.playerVisibility)
        {
            visibility.visible.fill([](const auto&) { return static_cast<unsigned char>(1); });
        }
    }
}
