#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/ThreatMap.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <functional>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    class UnitState;

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

        /**
         * Whether this unit has stopped shooting at that target because
         * nothing it fired ever moved its hit points, and how long that
         * lasts. See LandTargetProgress.
         */
        bool hasGivenUpOn(UnitId unit, UnitId target, GameTime now) const;

    private:
        int ticksSinceLastUpdate{0};

        /**
         * A target our hulls are firing at, and whether it is getting hurt.
         * A torpedo runs along the water and stops at whatever seabed rises
         * in front of it, so a ship in the shallows can fire at an
         * underwater extractor for the rest of the game and never touch it.
         * When a target's hit points have not moved for
         * navalStalledAttackSeconds the attempt number goes up, and each
         * ship that has not yet answered that attempt is moved to deep water
         * on another side of the target to try from there.
         */
        struct NavalTargetProgress
        {
            unsigned int hitPoints{0};
            GameTime since{0};
            int attempt{0};
        };
        mutable std::map<unsigned int, NavalTargetProgress> navalTargetProgress;
        /** The attempt each ship last answered, by target: ship id, then target id. */
        mutable std::map<std::pair<unsigned int, unsigned int>, int> navalAttemptAnswered;
        /** A ship on its way to a new firing position: where, and until when. */
        mutable std::map<unsigned int, std::pair<SimVector, GameTime>> navalRepositioning;

        /**
         * The same question asked on land, where the answer is usually
         * ground rather than water: a shell fired at something standing
         * above you hits the slope, and the unit firing it will go on
         * firing for the rest of the game because the target is there,
         * in range, and never gets any less alive. Reported from a replay
         * as "a lot of units will all repeatedly shoot at a structure
         * their projectiles cant reach for ages and get stuck in that loop
         * until another unit is able to destroy it".
         *
         * Both halves are per unit and target. Measured: a clock that
         * starts when the attack is ORDERED throws the game away -- eight
         * seeds, 67.9 units against 92.9 with the rule off -- because a unit
         * walking across the map to its target has not fired a shot when the
         * window runs out, so it gives up on everything and mills about. The
         * clock therefore only runs while the unit is within its own
         * weapon's reach of the target, which is the nearest thing the AI
         * has to "we are shooting at it and it is not working".
         */
        struct LandTargetProgress
        {
            unsigned int hitPoints{0};
            GameTime since{0};
        };
        /** Unit, then target: one unit's own attempt on one target. */
        mutable std::map<std::pair<unsigned int, unsigned int>, LandTargetProgress> landTargetProgress;
        /** Unit, then target: when that unit may look at that target again. */
        mutable std::map<std::pair<unsigned int, unsigned int>, GameTime> landTargetGivenUp;
        /**
         * Unit, then target: how many times that unit has moved to try to
         * get a shot at it.
         *
         * Giving up is the second answer and not the first. A unit whose
         * shots are not arriving is usually in the wrong place rather than
         * facing the wrong enemy -- the ground is in the way, or a wall of
         * wrecks is, or the shot has not the elevation for it -- and all of
         * those have the same fix, which is to stand somewhere else. See
         * AiTuningProfile::stalledAttackRepositionTries.
         */
        mutable std::map<std::pair<unsigned int, unsigned int>, int> landTargetRepositions;

        /**
         * The open water nearest a place the fleet is sailing towards, and
         * the two places it was worked out for. See updateNavy: the search
         * behind it walks every shipyard site on the map.
         */
        struct NavalWaypointMemo
        {
            SimVector towards;
            SimVector home;
            std::optional<SimVector> site;
        };
        mutable std::optional<NavalWaypointMemo> navalWaypointMemo;

        /**
         * Where an idle hull waits, and the yard it was worked out for. See
         * updateNavy and AiTuningProfile::navalRallyDistance.
         */
        struct NavalRallyMemo
        {
            SimVector home;
            std::optional<SimVector> station;
        };
        mutable std::optional<NavalRallyMemo> navalRallyMemo;

        /**
         * Open water navalRallyDistance off the yard, for hulls with nothing
         * to do. std::nullopt when the yard sits in a notch with no clear
         * water round it at that distance, in which case the caller falls
         * back to the yard itself and behaves as it did before this existed.
         */
        std::optional<SimVector> navalRallyPoint(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const SimVector& navalHome) const;

        void updateCommanderSafety(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const;
        /**
         * Whether the commander, about to fight, stays on the frame it is
         * putting up instead; when it goes, hands the frame to a builder if
         * one is free. See AiTuningProfile::commanderKeepsFrames.
         */
        bool commanderStaysOnFrame(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            AiBlackboard& bb,
            const UnitState& commander,
            float threatMetal,
            std::vector<PlayerCommand>& outCommands) const;

        void updateRallyPoint(const AiTuningProfile& profile, AiBlackboard& bb) const;
        std::optional<UnitId> nearestKnownEnemy(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance, bool airOnly = false, const std::function<bool(UnitId)>& skip = nullptr) const;

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
        std::optional<UnitId> nearestNavalEnemy(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance) const;

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
            PlayerId aiOwner,
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
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands) const;

        /**
         * The enemy's outlying economy: a building of theirs away from their
         * base with nothing covering it. Nothing to raid returns nothing.
         */
        std::optional<UnitId> chooseRaidTarget(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, const AiBlackboard& bb, const ThreatMap& threatMap) const;
    };
}
