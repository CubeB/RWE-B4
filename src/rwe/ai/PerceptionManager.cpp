#include "PerceptionManager.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    void PerceptionManager::refresh(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, AiBlackboard& bb) const
    {
        // Forget units that are gone, or that we can see are no longer where we left them.
        for (auto it = bb.knownEnemies.begin(); it != bb.knownEnemies.end();)
        {
            auto unitRef = sim.tryGetUnitState(it->second.unitId);
            auto dead = !unitRef || unitRef->get().isDead();
            auto lookedAndGone = !dead
                && !sim.canDetectUnit(aiOwner, it->second.unitId)
                && sim.isVisibleTo(aiOwner, it->second.lastKnownPosition);
            if (dead || lookedAndGone)
            {
                it = bb.knownEnemies.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Record everything we can currently detect.
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.isDead() || unit.isOwnedBy(aiOwner))
            {
                continue;
            }
            if (!profile.cheatModeOmniscient && !sim.canDetectUnit(aiOwner, unitId))
            {
                continue;
            }
            const auto& def = sim.unitDefinitions.at(unit.unitType);
            auto armed = !def.weapon1.empty() || !def.weapon2.empty() || !def.weapon3.empty();
            bb.knownEnemies[unitId.value] = KnownEnemy{unitId, unit.unitType, unit.position, bb.now, !def.isMobile, armed};
        }

        // Where do we think the enemy lives? The centroid of their known buildings.
        bb.enemyBasePosition.reset();
        SimVector sum(0_ss, 0_ss, 0_ss);
        int buildings = 0;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (enemy.isBuilding)
            {
                sum = sum + enemy.lastKnownPosition;
                ++buildings;
            }
        }
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
