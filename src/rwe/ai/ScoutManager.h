#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/ThreatMap.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /** Keeps one combat unit wandering to the ground we have seen least recently. */
    class ScoutManager
    {
    public:
        void update(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const ThreatMap& threatMap,
            AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands);

    private:
        int ticksSinceLastUpdate{0};
    };
}
