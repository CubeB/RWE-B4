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
        std::optional<UnitId> nearestKnownEnemy(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance, bool airOnly = false) const;

        /**
         * As nearestKnownEnemy, but only an enemy MapIntel::sameWaterBody
         * says is on the same body of water as `from` -- the coarse test
         * the type's own comment says is right for "is this worth going
         * to". ArmyManager::update is never handed a ReachabilityMap (S:13.2
         * step 5, the naval layer wiring, is still someone else's work in
         * progress), so this is what stands in for isNavalReachable until
         * that lands; it degrades safely on its own terms, since
         * bb.mapIntel.valid false or sameWaterBody's own "0 means dry or
         * off the map" answer both just find nothing to shoot at.
         */
        std::optional<UnitId> nearestNavalEnemy(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance) const;

        /**
         * Ships hold the coast at the shipyard that built them and fight
         * only what nearestNavalEnemy says is on the same water -- never
         * the rally point or an attack target, both of which sit on land.
         * Nothing here ever sees bb.combatUnits or is seen by anything that
         * walks it: bb.navalCombatUnits is EconomyManager's own bucket for
         * exactly this reason.
         */
        void updateNavy(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands) const;

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

        /**
         * The commander goes at whatever is besieging a production site,
         * when nothing else of ours is left to send.
         *
         * Off unless AiTuningProfile::commanderAnswersHarassment says
         * otherwise; see that knob for why it is not on by default.
         */
        void answerHarassmentWithCommander(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands) const;

        /**
         * The enemy's outlying economy: a building of theirs away from their
         * base with nothing covering it. Nothing to raid returns nothing.
         */
        std::optional<UnitId> chooseRaidTarget(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, const ThreatMap& threatMap) const;
    };
}
