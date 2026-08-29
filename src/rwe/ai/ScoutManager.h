#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/ReachabilityMap.h>
#include <rwe/ai/ThreatMap.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * Sends the scouts out to the ground we have seen least recently:
     * scout planes and scout vehicles when we have them, otherwise one
     * combat unit pressed into the job. Planes fly a string of waypoints;
     * ground scouts keep to land they can walk to and away from known guns.
     */
    class ScoutManager
    {
    public:
        void update(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const ThreatMap& threatMap,
            const ReachabilityMap& reachability,
            AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands);

    private:
        int ticksSinceLastUpdate{0};

        void sendScout(
            const GameSimulation& sim,
            const ThreatMap& threatMap,
            const ReachabilityMap& reachability,
            AiBlackboard& bb,
            UnitId scoutId,
            std::vector<PlayerCommand>& outCommands) const;
    };
}
