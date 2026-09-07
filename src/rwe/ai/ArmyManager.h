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
     * Commands the combat units as one body: gather at the rally point,
     * defend the base when enemies show up, and attack the most valuable
     * known enemy ground when the army is big enough.
     */
    class ArmyManager
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

        void updateRallyPoint(const AiTuningProfile& profile, AiBlackboard& bb) const;
        std::optional<UnitId> nearestKnownEnemy(const GameSimulation& sim, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance, bool airOnly = false) const;

        /**
         * Mobile anti-air covers the base instead of joining the attack.
         * Shoots anything airborne in reach, and otherwise walks back to the
         * base anchor -- cover that has wandered off is not cover.
         */
        void updateAntiAir(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands) const;
    };
}
