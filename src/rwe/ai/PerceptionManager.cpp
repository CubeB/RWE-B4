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
        int armedAir = 0;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (enemy.isAir)
            {
                ++bb.knownEnemyAirCount;
                if (enemy.isArmed)
                {
                    ++armedAir;
                }
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
        bb.enemyArmedAirPeak = std::max(bb.enemyArmedAirPeak, armedAir);
        bb.enemyAirThreat = bb.knownEnemyAirCount > 0
            || (bb.lastEnemyAirSeenAt && bb.now.value - bb.lastEnemyAirSeenAt->value <= AirThreatMemoryTicks);
        if (buildings > 0)
        {
            bb.enemyBasePosition = SimVector(sum.x / SimScalar(static_cast<float>(buildings)), 0_ss, sum.z / SimScalar(static_cast<float>(buildings)));
        }

        // What the radar says is coming. A contact is an enemy we cannot see
        // standing where our radar reaches -- the dot on a player's minimap,
        // with no name on it -- and one that is inside the warning ring and
        // nearer than it was last pass is closing. Enough of those and the
        // army is told where from. Seen enemies that are armed, mobile and
        // closing count too: a column in plain view is no less a column.
        if (bb.incomingAttackFrom && bb.now.value >= bb.incomingAttackUntil.value)
        {
            bb.incomingAttackFrom.reset();
        }
        if (bb.baseAnchor && profile.radarWarningRings > 0.0f && bb.now.value >= bb.radarSampleAt.value + SimTicksPerSecond)
        {
            bb.radarSampleAt = bb.now;
            auto ring = profile.defendRadius * SimScalar(profile.radarWarningRings);
            auto ringSquared = ring * ring;
            std::map<unsigned int, SimScalar> distances;
            float sumX = 0.0f;
            float sumZ = 0.0f;
            int closing = 0;
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unit.isDead() || unit.isOwnedBy(aiOwner))
                {
                    continue;
                }
                auto distance = bb.baseAnchor->distanceSquared(unit.position);
                if (distance > ringSquared)
                {
                    continue;
                }
                const auto& def = sim.unitDefinitions.at(unit.unitType);
                if (!def.isMobile)
                {
                    continue;
                }
                bool seen = profile.cheatModeOmniscient || sim.canSeeUnit(aiOwner, unitId);
                if (seen)
                {
                    if (def.weapon1.empty() && def.weapon2.empty() && def.weapon3.empty())
                    {
                        continue;
                    }
                }
                else if (!sim.isOnRadarOf(aiOwner, unit.position))
                {
                    continue;
                }
                distances[unitId.value] = distance;
                auto before = bb.radarContactDistance.find(unitId.value);
                // Squared distances, so the margin is taken on the roots:
                // eight units in the second is a walk, less is milling about.
                if (before != bb.radarContactDistance.end() && rweSqrt(distance) + 8_ss < rweSqrt(before->second))
                {
                    ++closing;
                    sumX += unit.position.x.value;
                    sumZ += unit.position.z.value;
                }
            }
            bb.radarContactDistance = std::move(distances);
            if (closing >= profile.radarWarningMinContacts)
            {
                auto count = static_cast<float>(closing);
                bb.incomingAttackFrom = SimVector(SimScalar(sumX / count), 0_ss, SimScalar(sumZ / count));
                bb.incomingAttackUntil = GameTime(bb.now.value + (10u * SimTicksPerSecond));
            }
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
