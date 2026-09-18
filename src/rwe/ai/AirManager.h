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

    /**
     * The air arm: fighters and bombers.
     *
     * Nothing has ever commanded an aircraft that can shoot, because the air
     * plant has only ever built a scout. Neither type reaches combatUnits --
     * that list deliberately excludes anything that flies, since an escort
     * that leaves with the army is not cover -- so this pass owns them
     * outright and finds them by type each time rather than adding two more
     * lists to the economy sweep.
     */
    class AirManager
    {
    public:
        void update(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const ThreatMap& threatMap,
            AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands);

    private:
        int ticksSinceLastUpdate{0};
    };
}
