#include "PerceptionManager.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <map>

namespace rwe
{
    std::optional<std::reference_wrapper<const UnitState>>
        contactStillStanding(const GameSimulation& sim, const KnownEnemy& enemy)
    {
        auto unitRef = sim.tryGetUnitState(enemy.unitId);
        if (!unitRef || unitRef->get().isDead())
        {
            return std::nullopt;
        }
        return unitRef;
    }

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
        // Someone on our own team is not an enemy. Nothing else in the AI
        // asks, so an ally used to be recorded as a contact, counted into the
        // centroid of where "the enemy" lives, and offered to the army as
        // something to walk at. In a free-for-all every teamId is empty and
        // this changes nothing, which is why it went unnoticed.
        auto alliedWith = [&](PlayerId other) {
            const auto& ours = sim.getPlayer(aiOwner);
            const auto& theirs = sim.getPlayer(other);
            return ours.teamId && theirs.teamId && *ours.teamId == *theirs.teamId;
        };

        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.isDead() || unit.isOwnedBy(aiOwner))
            {
                continue;
            }
            if (alliedWith(unit.owner))
            {
                continue;
            }
            if (!profile.cheatModeOmniscient && !sim.canSeeUnit(aiOwner, unitId))
            {
                continue;
            }
            const auto& def = sim.unitDefinitions.at(unit.unitType);
            auto armed = !def.weapon1.empty() || !def.weapon2.empty() || !def.weapon3.empty();
            bb.knownEnemies[unitId.value] = KnownEnemy{unitId, unit.unitType, unit.position, bb.now, !def.isMobile, armed, def.canFly, def.commander, unit.owner};
            if (def.canFly)
            {
                bb.lastEnemyAirSeenAt = bb.now;
            }
        }

        // Where do we think the enemy lives? The centroid of their known
        // buildings -- kept per owner, because with more than one enemy the
        // centroid of all of them together is a point none of them lives at.
        // Counting their aircraft on the same walk, because that is what
        // decides whether anti-air is worth any metal.
        bb.enemyBasePosition.reset();
        bb.enemyCommanderPosition.reset();
        bb.knownEnemyAirCount = 0;
        SimVector sum(0_ss, 0_ss, 0_ss);
        int buildings = 0;
        int armedAir = 0;
        // Keyed by the raw player id, and a std::map, so the walk below is in
        // player order on every peer. The AI is part of the simulation and
        // what it decides here moves units.
        struct EnemyPlace
        {
            SimVector sum{0_ss, 0_ss, 0_ss};
            int buildings{0};
            std::optional<SimVector> commander;
        };
        std::map<unsigned int, EnemyPlace> places;
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
            auto& place = places[enemy.owner.value];
            if (enemy.isBuilding)
            {
                sum = sum + enemy.lastKnownPosition;
                ++buildings;
                place.sum = place.sum + enemy.lastKnownPosition;
                ++place.buildings;
            }
            // The first one in the walk, which is the lowest unit id, so a
            // player with two commanders is answered in an order that does
            // not depend on anything but the simulation. knownEnemies is
            // keyed by that id for the same reason.
            if (enemy.isCommander && !place.commander)
            {
                place.commander = enemy.lastKnownPosition;
            }
        }

        // Where we believe each of them lives: the middle of their buildings,
        // or their commander if that is all we have found of them.
        auto placeOf = [](const EnemyPlace& place) -> std::optional<SimVector> {
            if (place.buildings > 0)
            {
                return SimVector(
                    place.sum.x / SimScalar(static_cast<float>(place.buildings)),
                    0_ss,
                    place.sum.z / SimScalar(static_cast<float>(place.buildings)));
            }
            return place.commander;
        };

        // Aircraft are fast and rarely sit still to be counted, so the
        // sighting has to outlive the sight of it. Without the memory the AI
        // would start a tower, lose the bomber, drop the tower off its wanted
        // list, and be defenceless again by the time the bomber came back.
        bb.enemyArmedAirPeak = std::max(bb.enemyArmedAirPeak, armedAir);
        bb.enemyAirThreat = bb.knownEnemyAirCount > 0
            || (bb.lastEnemyAirSeenAt && bb.now.value - bb.lastEnemyAirSeenAt->value <= AirThreatMemoryTicks);

        // Who the war is against. The nearest of them, and then that one
        // until it is finished: see AiTuningProfile::focusOneEnemy for why
        // averaging three enemies into one is not a decision about where to
        // concentrate, and focusSwitchMargin for why it has to stick.
        auto stillFighting = [&](PlayerId player) {
            return player.value < sim.players.size()
                && sim.players[player.value].status == GamePlayerStatus::Alive;
        };

        if (!profile.focusOneEnemy)
        {
            bb.focusEnemy.reset();
        }
        else if (bb.baseAnchor)
        {
            std::optional<PlayerId> nearest;
            SimScalar nearestDistance = 0_ss;
            for (const auto& [ownerValue, place] : places)
            {
                auto owner = PlayerId(ownerValue);
                auto centre = placeOf(place);
                if (!centre || !stillFighting(owner))
                {
                    continue;
                }
                auto d = bb.baseAnchor->distance(*centre);
                if (!nearest || d < nearestDistance)
                {
                    nearestDistance = d;
                    nearest = owner;
                }
            }

            std::optional<SimScalar> currentDistance;
            if (bb.focusEnemy && stillFighting(*bb.focusEnemy))
            {
                auto it = places.find(bb.focusEnemy->value);
                if (it != places.end())
                {
                    if (auto centre = placeOf(it->second))
                    {
                        currentDistance = bb.baseAnchor->distance(*centre);
                    }
                }
            }

            // Keep the one we are already fighting unless it is gone, or
            // unless another is nearer by more than the margin. Without that
            // second clause the focus changes hands every time a scout finds
            // a building and moves a centroid, and a wave that is given a new
            // objective every pass arrives nowhere.
            if (!currentDistance)
            {
                bb.focusEnemy = nearest;
            }
            else if (nearest && nearestDistance + profile.focusSwitchMargin < *currentDistance)
            {
                bb.focusEnemy = nearest;
            }
        }

        // Where the enemy lives, and where its commander was. Of the one we
        // are fighting where there is one; of all of them together otherwise,
        // which is what this always was and is right for a single opponent.
        if (bb.focusEnemy)
        {
            auto it = places.find(bb.focusEnemy->value);
            if (it != places.end())
            {
                if (it->second.buildings > 0)
                {
                    bb.enemyBasePosition = SimVector(
                        it->second.sum.x / SimScalar(static_cast<float>(it->second.buildings)),
                        0_ss,
                        it->second.sum.z / SimScalar(static_cast<float>(it->second.buildings)));
                }
                bb.enemyCommanderPosition = it->second.commander;
            }
        }
        else
        {
            if (buildings > 0)
            {
                bb.enemyBasePosition = SimVector(sum.x / SimScalar(static_cast<float>(buildings)), 0_ss, sum.z / SimScalar(static_cast<float>(buildings)));
            }
            for (const auto& [_, place] : places)
            {
                if (place.commander)
                {
                    bb.enemyCommanderPosition = place.commander;
                    break;
                }
            }
        }

        // Which of our harassed factories still has a gun sitting on it.
        //
        // EconomyManager has already said where our frames have been dying;
        // this is the other half of the same question, and it is asked here
        // because this is the pass that knows what we can see. A factory
        // that lost hulls a minute ago and has nothing near it now is not
        // besieged, and its queue goes back to being topped up.
        //
        // Any armed enemy counts, aircraft included, and that differs from
        // BuildManager::siteUnderEnemyGuns on purpose (issue #152). That test
        // leaves aircraft out because an aeroplane is over a site for a
        // moment and somewhere else by the time a builder gets there, so
        // refusing ground on account of one refuses the whole map in turn.
        // This one is only asked of a factory that has already lost frames on
        // its own pad, and there an armed aeroplane over it is the likeliest
        // thing doing it: a bomber kills a frame as surely as a gunboat does.
        // What follows from the answer lasts only as long as the aeroplane is
        // there -- the queue is left alone and the enemy counts as near the
        // base -- and enemiesNearBase already counts armed aircraft near the
        // anchor, so leaving them out here would be the odd one.
        bb.besiegedFactories.clear();
        auto harassRadiusSquared = profile.productionHarassRadius * profile.productionHarassRadius;
        if (profile.noticeProductionHarassment)
        {
            for (auto factoryId : bb.harassedFactories)
            {
                auto factoryRef = sim.tryGetUnitState(factoryId);
                if (!factoryRef || factoryRef->get().isDead())
                {
                    continue;
                }
                const auto& factoryPosition = factoryRef->get().position;
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (enemy.isArmed && factoryPosition.distanceSquared(enemy.lastKnownPosition) <= harassRadiusSquared)
                    {
                        bb.besiegedFactories.push_back(factoryId);
                        break;
                    }
                }
            }
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
        //
        // Measured from the base anchor, and -- since the thing being shot
        // at is not always the base -- from any production site of ours that
        // is under siege. Without the second the AI could lose an entire
        // production run to one gun and never enter Defend, never answer the
        // intruder and never put up a tower, because the anchor was a
        // thousand units away and perfectly quiet. See
        // AiTuningProfile::noticeProductionHarassment.
        //
        // One pass over knownEnemies rather than two, so the result stays in
        // raw-id order however many places contributed to it.
        bb.enemiesNearBase.clear();
        if (bb.baseAnchor || !bb.besiegedFactories.empty())
        {
            auto radiusSquared = profile.defendRadius * profile.defendRadius;
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                if (!enemy.isArmed)
                {
                    continue;
                }
                bool near = bb.baseAnchor && bb.baseAnchor->distanceSquared(enemy.lastKnownPosition) <= radiusSquared;
                for (auto factoryId = bb.besiegedFactories.begin(); !near && factoryId != bb.besiegedFactories.end(); ++factoryId)
                {
                    auto factoryRef = sim.tryGetUnitState(*factoryId);
                    near = factoryRef && factoryRef->get().position.distanceSquared(enemy.lastKnownPosition) <= harassRadiusSquared;
                }
                if (near)
                {
                    bb.enemiesNearBase.push_back(enemy.unitId);
                }
            }
        }
    }
}
