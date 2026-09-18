#include "PerceptionManager.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    void PerceptionManager::refresh(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, AiBlackboard& bb) const
    {
        // Forget an enemy only where a player would have seen it go.
        //
        // This used to drop a remembered unit the moment it died, wherever it
        // died. That is knowledge nobody has: a tank killed on the far side of
        // the map, out of our sight, would vanish off the AI's map at the
        // instant it blew up. A player keeps the marker and has to go and look
        // -- so the test is not "is it dead" but "are we watching the place we
        // last saw it", which covers both the unit dying in front of us and
        // the unit having quietly moved on.
        for (auto it = bb.knownEnemies.begin(); it != bb.knownEnemies.end();)
        {
            auto unitRef = sim.tryGetUnitState(it->second.unitId);
            auto gone = !unitRef || unitRef->get().isDead();

            bool forget;
            if (profile.cheatModeOmniscient)
            {
                // Brutal sees everything anyway, so a stale marker would only
                // be a lie it tells itself.
                forget = gone;
            }
            else
            {
                auto watching = sim.isVisibleTo(aiOwner, it->second.lastKnownPosition);
                auto stillThere = !gone && sim.canSeeUnit(aiOwner, it->second.unitId);
                forget = watching && !stillThere;
            }

            if (forget)
            {
                it = bb.knownEnemies.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Record everything we can currently see. The computer player and the
        // weapon scan read the same list in the original -- 0x40AA40 builds
        // it once per player and both walk it -- so the AI is held to the same
        // predicate, 0x465AC0, and does not get to act on radar contacts a
        // turret would not shoot at.
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.isDead() || unit.isOwnedBy(aiOwner))
            {
                continue;
            }
            if (!profile.cheatModeOmniscient && !sim.canSeeUnit(aiOwner, unitId))
            {
                continue;
            }
            const auto& def = sim.unitDefinitions.at(unit.unitType);
            auto armed = !def.weapon1.empty() || !def.weapon2.empty() || !def.weapon3.empty();
            bb.knownEnemies[unitId.value] = KnownEnemy{unitId, unit.unitType, unit.position, bb.now, !def.isMobile, armed, def.canFly};
            if (def.canFly)
            {
                bb.lastEnemyAirSeenAt = bb.now;
            }
        }

        // Where do we think the enemy lives? The centroid of their known
        // buildings. Counting their aircraft on the same walk, because that is
        // what decides whether anti-air is worth any metal.
        bb.enemyBasePosition.reset();
        bb.knownEnemyAirCount = 0;
        SimVector sum(0_ss, 0_ss, 0_ss);
        int buildings = 0;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (enemy.isAir)
            {
                ++bb.knownEnemyAirCount;
            }
            if (enemy.isBuilding)
            {
                sum = sum + enemy.lastKnownPosition;
                ++buildings;
            }
        }

        // Aircraft are fast and rarely sit still to be counted, so the
        // sighting has to outlive the sight of it. Without the memory the AI
        // would start a tower, lose the bomber, drop the tower off its wanted
        // list, and be defenceless again by the time the bomber came back.
        bb.enemyAirThreat = bb.knownEnemyAirCount > 0
            || (bb.lastEnemyAirSeenAt && bb.now.value - bb.lastEnemyAirSeenAt->value <= AirThreatMemoryTicks);
        if (buildings > 0)
        {
            bb.enemyBasePosition = SimVector(sum.x / SimScalar(static_cast<float>(buildings)), 0_ss, sum.z / SimScalar(static_cast<float>(buildings)));
        }

        // Anyone knocking on the door?
        bb.enemiesNearBase.clear();
        if (bb.baseAnchor)
        {
            auto radiusSquared = profile.defendRadius * profile.defendRadius;
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                if (enemy.isArmed && bb.baseAnchor->distanceSquared(enemy.lastKnownPosition) <= radiusSquared)
                {
                    bb.enemiesNearBase.push_back(enemy.unitId);
                }
            }
        }
    }
}
