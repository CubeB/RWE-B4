#include "ArmyManager.h"
#include <rwe/ai/PerceptionManager.h>
#include <algorithm>
#include <set>
#include <rwe/ai/AiMapBounds.h>
#include <rwe/ai/BuilderSafety.h>
#include <rwe/ai/LineOfFire.h>
#include <rwe/sim/GameSimulation.h>
#include <array>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>
#include <vector>

namespace rwe
{
    namespace
    {
        PlayerCommand moveCommand(UnitId unit, const SimVector& to)
        {
            return PlayerUnitCommand(unit, PlayerUnitCommand::IssueOrder(MoveOrder(to), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
        }

        PlayerCommand attackCommand(UnitId unit, UnitId target)
        {
            return PlayerUnitCommand(unit, PlayerUnitCommand::IssueOrder(AttackOrder(target), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
        }

        /**
         * Whether a remembered enemy is fresh enough to be shot at.
         *
         * PerceptionManager only stamps `lastSeen` on what the AI can
         * actually see, so this is "has our side laid eyes on it lately".
         * The marker itself is kept longer on purpose -- it is what sends a
         * scout back to look -- but a target nobody can see is not one the
         * original would have offered a weapon. See AiTuningProfile::
         * targetMemoryTicks and TOTALA-EXE.md S:10.
         */
        bool inSightRecently(const AiBlackboard& bb, const AiTuningProfile& profile, const KnownEnemy& enemy)
        {
            return bb.now.value <= enemy.lastSeen.value + static_cast<unsigned int>(profile.targetMemoryTicks);
        }

        bool isDgunning(const UnitState& unit, UnitId target)
        {
            if (unit.orders.empty())
            {
                return false;
            }
            auto dgun = std::get_if<DgunOrder>(&unit.orders.front());
            if (dgun == nullptr)
            {
                return false;
            }
            auto aimed = std::get_if<UnitId>(&dgun->target);
            return aimed != nullptr && *aimed == target;
        }

        bool isAttackingUnit(const UnitState& unit, UnitId target)
        {
            if (unit.orders.empty())
            {
                return false;
            }
            auto attack = std::get_if<AttackOrder>(&unit.orders.front());
            if (attack == nullptr)
            {
                return false;
            }
            auto attacked = std::get_if<UnitId>(&attack->target);
            return attacked != nullptr && *attacked == target;
        }

        /**
         * Whether a unit, standing where it is, could hurt anything with its
         * own guns. Below the waterline it cannot unless it carries a
         * waterweapon: the projectile collision test in GameSimulation stops
         * every other round the moment it is at or under sea level over water,
         * which is the original's rule too. A commander walking the seabed is
         * unarmed for as long as it is down there -- on Brain Coral, that is
         * the whole game.
         *
         * Judged from the unit's origin rather than its firing piece, which is
         * good enough for the case this exists for: a commander at a depth of
         * 47 to 75 is under the surface from head to foot.
         */
        bool canFireFrom(const GameSimulation& sim, const UnitState& unit)
        {
            if (unit.position.y >= sim.terrain.getSeaLevel())
            {
                return true;
            }
            auto defIt = sim.unitDefinitions.find(unit.unitType);
            if (defIt == sim.unitDefinitions.end())
            {
                return false;
            }
            for (const auto* name : {&defIt->second.weapon1, &defIt->second.weapon2, &defIt->second.weapon3})
            {
                if (name->empty())
                {
                    continue;
                }
                auto weaponIt = sim.weaponDefinitions.find(*name);
                if (weaponIt != sim.weaponDefinitions.end() && weaponIt->second.waterWeapon)
                {
                    return true;
                }
            }
            return false;
        }

        /**
         * A unit still under construction, which is not a target worth an
         * order. A frame spawns with nought hit points and dies quietly to any
         * hit, so the units' own fire-at-will finishes it without being asked.
         * And one on a factory's pad stands exactly where the factory does, so
         * a nearest-enemy search picks it ahead of the factory every time:
         * ordering the attack at the frame is how a raid kills the same cheap
         * frame over and over while the factory that keeps making it stands
         * untouched. Skipping it hands the order to the factory instead.
         */
        bool isNanoframe(const GameSimulation& sim, const UnitState& unit)
        {
            auto defIt = sim.unitDefinitions.find(unit.unitType);
            return defIt != sim.unitDefinitions.end() && unit.isBeingBuilt(defIt->second);
        }

        /**
         * The frame a builder is putting up at this moment, if any: the one
         * its nanolathe is on, the one its build order has placed, or the one
         * it has been told to finish.
         */
        std::optional<UnitId> frameInHand(const GameSimulation& sim, const UnitState& builder)
        {
            std::optional<UnitId> frameId;
            if (auto building = std::get_if<UnitBehaviorStateBuilding>(&builder.behaviourState); building != nullptr)
            {
                frameId = building->targetUnit;
            }
            else if (builder.buildOrderUnitId)
            {
                frameId = builder.buildOrderUnitId;
            }
            else if (!builder.orders.empty())
            {
                if (auto repair = std::get_if<RepairOrder>(&builder.orders.front()); repair != nullptr)
                {
                    frameId = repair->target;
                }
                else if (auto complete = std::get_if<CompleteBuildOrder>(&builder.orders.front()); complete != nullptr)
                {
                    frameId = complete->target;
                }
            }
            if (!frameId)
            {
                return std::nullopt;
            }
            auto frameRef = sim.tryGetUnitState(*frameId);
            if (!frameRef || frameRef->get().isDead() || !isNanoframe(sim, frameRef->get()))
            {
                return std::nullopt;
            }
            return frameId;
        }

        /**
         * How long a frame left alone from now would last before it rotted
         * away, in ticks. It loses a flat energy-point of its cost a tick,
         * whatever the unit (GameSimulation::updateNanoframeDecay,
         * TOTALA-EXE.md s93), so what stands of it lasts what is built of it
         * times its energy cost. The second's grace before the rot starts is
         * left out: it may be nearly spent.
         */
        unsigned int frameLifeTicks(const GameSimulation& sim, const UnitState& frame)
        {
            const auto& def = sim.unitDefinitions.at(frame.unitType);
            auto energyCost = static_cast<unsigned long long>(def.buildCostEnergy.value);
            if (energyCost == 0 || def.buildTime == 0)
            {
                return 0;
            }
            return static_cast<unsigned int>(static_cast<unsigned long long>(frame.buildTimeCompleted) * energyCost / def.buildTime);
        }

        bool isMovingTo(const UnitState& unit, const SimVector& destination)
        {
            if (unit.orders.empty())
            {
                return false;
            }
            auto move = std::get_if<MoveOrder>(&unit.orders.front());
            return move != nullptr && move->destination.distanceSquared(destination) < (64_ss * 64_ss);
        }

        /** Distance across the ground, ignoring height: units stand on terrain and targets are flat points. */
        SimScalar flatDistance(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
        }

        /**
         * Whether something of ours now stands, finished, at the site a
         * guard was asked for -- the construction it came to protect is
         * done.
         *
         * Judged by what stands there rather than by the builder's own
         * order queue. A BuildOrder BuildManager issues this tick is a
         * PlayerCommand, not yet applied to the builder's own orders --
         * that happens through the ordinary command pipeline, at the
         * earliest next tick, the same one tick of latency a human
         * player's command carries. Checking the order queue the same
         * tick the request was written released the guard before the
         * order had even landed, which is not the same bug in two places;
         * it is the one bug this function exists to avoid.
         *
         * Buildings only. A mobile unit -- the builder itself, arriving to
         * start the job -- is complete and stands right there too, and
         * counting it would report the job done the moment it began.
         */
        bool somethingFinishedStandsAt(const GameSimulation& sim, PlayerId owner, const SimVector& site)
        {
            for (const auto& [_, unit] : sim.units)
            {
                if (unit.owner != owner || !unit.isAlive())
                {
                    continue;
                }
                if (unit.position.distanceSquared(site) > (64_ss * 64_ss))
                {
                    continue;
                }
                const auto& def = sim.unitDefinitions.at(unit.unitType);
                // Not the builder itself: a mobile unit standing at its own
                // build site is not the sign the build is done, it is the
                // sign it has just arrived to start.
                if (!def.isMobile && !unit.isBeingBuilt(def))
                {
                    return true;
                }
            }
            return false;
        }
    }

    void ArmyManager::updateRallyPoint(const AiTuningProfile& profile, AiBlackboard& bb) const
    {
        if (!bb.baseAnchor)
        {
            bb.rallyPoint.reset();
            return;
        }
        // An army waiting on a sea lift gathers where the hull will load it
        // (AiTuningProfile::seaFerryMuster). TransportManager only publishes
        // the muster while that is the case.
        if (profile.seaFerryMuster && bb.armyNeedsFerry && bb.ferryMuster)
        {
            bb.rallyPoint = *bb.ferryMuster;
            return;
        }
        auto towards = bb.enemyBasePosition
            ? (*bb.enemyBasePosition - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss))
            : SimVector(1_ss, 0_ss, 0_ss);
        bb.rallyPoint = *bb.baseAnchor + (towards * profile.rallyDistance);
    }

    bool ArmyManager::hasGivenUpOn(UnitId unit, UnitId target, GameTime now) const
    {
        auto it = landTargetGivenUp.find(std::make_pair(unit.value, target.value));
        return it != landTargetGivenUp.end() && now.value < it->second.value;
    }

    std::optional<UnitId> ArmyManager::nearestKnownEnemy(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance, bool airOnly, const std::function<bool(UnitId)>& skip) const
    {
        std::optional<UnitId> best;
        auto bestDistanceSquared = maxDistance * maxDistance;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (airOnly && !enemy.isAir)
            {
                continue;
            }
            if (skip && skip(enemy.unitId))
            {
                continue;
            }
            if (!inSightRecently(bb, profile, enemy))
            {
                continue;
            }
            auto contact = contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy);
            if (!contact)
            {
                continue;
            }
            if (contact->unit && isNanoframe(sim, *contact->unit))
            {
                continue;
            }
            auto d = from.distanceSquared(enemy.lastKnownPosition);
            if (d <= bestDistanceSquared)
            {
                bestDistanceSquared = d;
                best = enemy.unitId;
            }
        }
        return best;
    }

    void ArmyManager::updateAntiAir(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands) const
    {
        // How far anti-air may drift from the base before it is called back.
        // Wide enough to chase a bomber across the base without being turned
        // round every pass; narrow enough that it is still cover.
        const SimScalar AntiAirLeash = 700_ss;

        for (auto unitId : bb.antiAirUnits)
        {
            auto unitRef = sim.tryGetUnitState(unitId);
            if (!unitRef)
            {
                continue;
            }
            const auto& unit = unitRef->get();

            // Anything airborne within reach gets shot at. The army's engage
            // radius, because it is the same question: is that close enough
            // to be worth leaving what I am doing.
            if (auto enemy = nearestKnownEnemy(sim, aiOwner, profile, bb, unit.position, profile.engageRadius, true))
            {
                if (!isAttackingUnit(unit, *enemy))
                {
                    outCommands.push_back(attackCommand(unitId, *enemy));
                }
                continue;
            }

            // Nothing to shoot at: go back and stand over the base. This is
            // the whole reason anti-air is kept out of combatUnits -- left in
            // the army it would walk off with the attack, and the base it was
            // built to cover would be open again.
            if (!bb.baseAnchor)
            {
                continue;
            }
            if (unit.position.distanceSquared(*bb.baseAnchor) <= AntiAirLeash * AntiAirLeash)
            {
                continue;
            }
            if (!isMovingTo(unit, *bb.baseAnchor))
            {
                outCommands.push_back(moveCommand(unitId, *bb.baseAnchor));
            }
        }
    }

    std::optional<UnitId> ArmyManager::nearestNavalEnemy(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const SimVector& from,
        SimScalar maxDistance) const
    {
        if (!bb.mapIntel.valid)
        {
            return std::nullopt;
        }
        std::optional<UnitId> best;
        auto bestDistanceSquared = maxDistance * maxDistance;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (!inSightRecently(bb, profile, enemy))
            {
                continue;
            }
            if (!sameWaterBody(bb.mapIntel, sim.terrain, from, enemy.lastKnownPosition))
            {
                continue;
            }
            auto contact = contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy);
            if (!contact)
            {
                continue;
            }
            if (contact->unit && isNanoframe(sim, *contact->unit))
            {
                continue;
            }
            auto d = from.distanceSquared(enemy.lastKnownPosition);
            if (d <= bestDistanceSquared)
            {
                bestDistanceSquared = d;
                best = enemy.unitId;
            }
        }
        return best;
    }

    namespace
    {
        /**
         * What the commander's D-gun should be fired at now, if anything: the
         * nearest armed enemy in the weapon's reach, when the energy for a
         * shot is in the bank and nothing of ours stands near the line of
         * fire. The weapon is slot 2 -- Weapon3 -- because that is the slot
         * DgunOrder fires (UnitBehaviorService::handleDgunOrder).
         */
        /** The longest reach of any weapon the definition names; zero for an unarmed one. */
        SimScalar longestWeaponRange(const GameSimulation& sim, const UnitDefinition& def)
        {
            SimScalar best = 0_ss;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                auto it = weaponName.empty() ? sim.weaponDefinitions.end() : sim.weaponDefinitions.find(weaponName);
                if (it != sim.weaponDefinitions.end())
                {
                    best = rweMax(best, it->second.maxRange);
                }
            }
            return best;
        }

        /**
         * Where a unit that outranges its target should stand: just outside
         * the target's own reach, straight back from it. Nothing when it is
         * already there, when the target outranges us, or when the target
         * cannot move (kiteWithLongerRange).
         */
        std::optional<SimVector> kiteBackFrom(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const UnitState& unit,
            const UnitDefinition& def,
            UnitId targetId)
        {
            if (!profile.kiteWithLongerRange)
            {
                return std::nullopt;
            }
            auto targetRef = sim.tryGetUnitState(targetId);
            if (!targetRef)
            {
                return std::nullopt;
            }
            const auto& target = targetRef->get();
            auto targetDefIt = sim.unitDefinitions.find(target.unitType);
            if (targetDefIt == sim.unitDefinitions.end() || !targetDefIt->second.isMobile)
            {
                return std::nullopt;
            }
            auto ours = longestWeaponRange(sim, def);
            auto theirs = longestWeaponRange(sim, targetDefIt->second);
            if (ours <= theirs + profile.kiteRangeMargin)
            {
                return std::nullopt;
            }
            auto offset = unit.position - target.position;
            offset.y = 0_ss;
            auto distance = rweSqrt(offset.lengthSquared());
            auto standOff = theirs + profile.kiteRangeMargin;
            if (distance >= standOff)
            {
                return std::nullopt;
            }
            auto away = offset.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
            return clampInsideVisibleMap(sim.terrain, target.position + (away * standOff), 64_ss);
        }

        /**
         * The longest reach among a unit's weapons that could hit the target
         * where it stands, asked of the simulation's own eligibility test
         * (GameSimulation::weaponCanHitUnit, 0x49ABB0): no torpedo at
         * something out of the water, nothing but a torpedo at a submerged
         * hull, an anti-air weapon only at something flying. A destroyer's
         * depth charge is not what it can hit a floating tower's gunner
         * with, so it must not be what sets its distance.
         */
        SimScalar longestRangeAgainst(const GameSimulation& sim, const UnitState& shooter, const UnitState& target)
        {
            const auto& def = sim.unitDefinitions.at(shooter.unitType);
            SimScalar best = 0_ss;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                auto it = weaponName.empty() ? sim.weaponDefinitions.end() : sim.weaponDefinitions.find(weaponName);
                if (it == sim.weaponDefinitions.end() || !sim.weaponCanHitUnit(it->second, shooter, target))
                {
                    continue;
                }
                best = rweMax(best, it->second.maxRange);
            }
            return best;
        }

        /**
         * Where a hull that outranges what it is attacking should lie: near
         * the outer edge of its own reach, straight back from the target,
         * and never inside the target's (issue #191). Nothing when it is
         * already that far out, when it has nothing that can hit the target
         * -- a submarine at a shipyard's dry corner -- when the target
         * reaches as far, or when the water there is too shallow for the
         * hull or is some other sea.
         *
         * Unlike kiteBackFrom this stands off from buildings too. Backing a
         * land unit away from a tower is walking away from the job; a hull's
         * whole advantage over a torpedo launcher or a floating tower is that
         * it can choose its distance.
         */
        std::optional<SimVector> hullStandOff(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const UnitState& ship,
            const UnitDefinition& shipDef,
            UnitId targetId)
        {
            if (!profile.kiteWithLongerRange || !profile.navalStandOff)
            {
                return std::nullopt;
            }
            auto targetRef = sim.tryGetUnitState(targetId);
            if (!targetRef)
            {
                return std::nullopt;
            }
            const auto& target = targetRef->get();
            auto targetDefIt = sim.unitDefinitions.find(target.unitType);
            if (targetDefIt == sim.unitDefinitions.end())
            {
                return std::nullopt;
            }
            const auto seaLevel = sim.terrain.getSeaLevel();

            auto ours = longestRangeAgainst(sim, ship, target);
            if (ours <= 0_ss)
            {
                return std::nullopt;
            }
            auto theirs = longestRangeAgainst(sim, target, ship);
            if (ours <= theirs + profile.kiteRangeMargin)
            {
                return std::nullopt;
            }

            auto offset = ship.position - target.position;
            offset.y = 0_ss;
            auto distance = rweSqrt(offset.lengthSquared());
            auto standOff = rweMax(theirs + profile.kiteRangeMargin, ours - profile.kiteRangeMargin);
            if (distance >= standOff)
            {
                return std::nullopt;
            }
            auto away = offset.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
            auto spot = clampInsideVisibleMap(sim.terrain, target.position + (away * standOff), 64_ss);
            auto groundAtSpot = sim.terrain.tryGetHeightAt(spot.x, spot.z);
            auto draught = SimScalar(static_cast<float>(sim.getAdHocMovementClass(shipDef.movementCollisionInfo).minWaterDepth));
            if (!groundAtSpot || *groundAtSpot > seaLevel - draught || !sameWaterBody(bb.mapIntel, sim.terrain, ship.position, spot))
            {
                return std::nullopt;
            }
            spot.y = seaLevel;
            return spot;
        }

        std::optional<UnitId> chooseDgunTarget(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            UnitId commanderId,
            const UnitState& commander,
            const UnitDefinition& commanderDef)
        {
            if (!commanderDef.canDgun || commanderDef.weapon3.empty())
            {
                return std::nullopt;
            }
            auto weaponIt = sim.weaponDefinitions.find(commanderDef.weapon3);
            if (weaponIt == sim.weaponDefinitions.end())
            {
                return std::nullopt;
            }
            const auto& weapon = weaponIt->second;
            if (sim.getPlayer(aiOwner).energy.value < weapon.energyPerShot.value)
            {
                return std::nullopt;
            }
            const auto reachSquared = weapon.maxRange * weapon.maxRange;
            std::optional<UnitId> best;
            SimVector bestPosition;
            SimScalar bestDistanceSquared = 0_ss;
            float bestMetal = 0.0f;
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                if (!enemy.isArmed || enemy.isAir || !inSightRecently(bb, profile, enemy))
                {
                    continue;
                }
                auto contact = contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy);
                if (!contact || (contact->unit && isNanoframe(sim, *contact->unit)))
                {
                    continue;
                }
                const auto& position = enemy.lastKnownPosition;
                if (sim.terrain.getHeightAt(position.x, position.z) < sim.terrain.getSeaLevel())
                {
                    continue;
                }
                auto dx = position.x - commander.position.x;
                auto dz = position.z - commander.position.z;
                auto distanceSquared = (dx * dx) + (dz * dz);
                if (distanceSquared > reachSquared)
                {
                    continue;
                }
                // The most expensive thing in reach (dgunByValue), the
                // nearest of two that cost the same; or simply the nearest
                // with the knob off.
                auto metal = 0.0f;
                if (auto defIt = sim.unitDefinitions.find(enemy.unitType); defIt != sim.unitDefinitions.end())
                {
                    metal = defIt->second.buildCostMetal.value;
                }
                bool better;
                if (!best)
                {
                    better = true;
                }
                else if (profile.dgunByValue && metal != bestMetal)
                {
                    better = metal > bestMetal;
                }
                else
                {
                    better = distanceSquared < bestDistanceSquared;
                }
                if (better)
                {
                    best = enemy.unitId;
                    bestPosition = position;
                    bestDistanceSquared = distanceSquared;
                    bestMetal = metal;
                }
            }
            if (!best)
            {
                return std::nullopt;
            }

            // Nothing of ours within 48 of the line from the commander out
            // to the end of the weapon's reach in that direction.
            const auto direction = SimVector(bestPosition.x - commander.position.x, 0_ss, bestPosition.z - commander.position.z).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
            const auto lineEnd = weapon.maxRange + 32_ss;
            const auto clearanceSquared = 48_ss * 48_ss;
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unit.owner != aiOwner || unitId == commanderId || unit.isDead())
                {
                    continue;
                }
                const SimVector offset(unit.position.x - commander.position.x, 0_ss, unit.position.z - commander.position.z);
                const auto along = offset.dot(direction);
                if (along < 0_ss || along > lineEnd)
                {
                    continue;
                }
                if (offset.lengthSquared() - (along * along) < clearanceSquared)
                {
                    return std::nullopt;
                }
            }
            return best;
        }
    }

    bool ArmyManager::commanderStaysOnFrame(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        AiBlackboard& bb,
        const UnitState& commander,
        float threatMetal,
        std::vector<PlayerCommand>& outCommands) const
    {
        auto frameId = frameInHand(sim, commander);
        if (!frameId)
        {
            bb.commanderKeptFrame.reset();
            return false;
        }
        bb.commanderReturnFrame = frameId;
        const auto& frame = sim.getUnitState(*frameId);
        const auto awayTicks = static_cast<unsigned int>(std::max(0, profile.commanderFrameAbsenceSeconds)) * SimTicksPerSecond;
        if (frameLifeTicks(sim, frame) >= awayTicks)
        {
            // It will keep while the commander is away.
            bb.commanderKeptFrame.reset();
            return false;
        }

        // Somebody else to do the fighting: armed units of ours near enough
        // to be there first, in id order.
        const auto coverSquared = profile.commanderFrameCoverRadius * profile.commanderFrameCoverRadius;
        float cover = 0.0f;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive() || unitId == *bb.commanderUnitId)
            {
                continue;
            }
            const auto& def = sim.unitDefinitions.at(unit.unitType);
            if (!def.canAttack || def.canFly || unit.isBeingBuilt(def))
            {
                continue;
            }
            if (unit.position.distanceSquared(commander.position) <= coverSquared)
            {
                cover += def.buildCostMetal.value;
            }
        }
        if (cover >= threatMetal)
        {
            if (bb.commanderKeptFrame != frameId)
            {
                LOG_INFO << "AI player " << aiOwner.value << ": the commander stays on its " << frame.unitType << " frame " << frameId->value
                         << " and leaves the fight to the army (" << static_cast<int>(cover) << " metal against " << static_cast<int>(threatMetal) << ")";
                sim.eventLog.event(sim.gameTime.value, "army_commander_stays")
                    .set("player", aiOwner.value)
                    .set("unit", bb.commanderUnitId ? bb.commanderUnitId->value : 0u)
                    .set("frame", frameId->value)
                    .set("subject", frame.unitType)
                    .set("cover", static_cast<int>(cover))
                    .set("threat", static_cast<int>(threatMetal))
                    .set("why", "army_covers")
                    .detail("the commander stays on its frame and leaves the fight to the army");
            }
            bb.commanderKeptFrame = frameId;
            return true;
        }
        bb.commanderKeptFrame.reset();

        // Nobody to fight: the commander goes. Somebody to finish the frame,
        // then -- the nearest construction unit doing nothing that matters,
        // which is nothing at all, guarding, patrolling or walking.
        const auto handoverSquared = profile.commanderFrameHandoverRadius * profile.commanderFrameHandoverRadius;
        std::optional<UnitId> helper;
        SimScalar helperDistance = 0_ss;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive() || unitId == *bb.commanderUnitId || unit.heldByMission)
            {
                continue;
            }
            const auto& def = sim.unitDefinitions.at(unit.unitType);
            if (!def.builder || !def.isMobile || def.commander || !def.canReclamate || unit.isBeingBuilt(def))
            {
                continue;
            }
            if (!unit.orders.empty())
            {
                const auto& order = unit.orders.front();
                if (auto repair = std::get_if<RepairOrder>(&order); repair != nullptr && repair->target == *frameId)
                {
                    // Already on it.
                    return false;
                }
                if (!std::holds_alternative<GuardOrder>(order) && !std::holds_alternative<PatrolOrder>(order) && !std::holds_alternative<MoveOrder>(order))
                {
                    continue;
                }
            }
            auto distance = unit.position.distanceSquared(frame.position);
            if (distance <= handoverSquared && (!helper || distance < helperDistance))
            {
                helper = unitId;
                helperDistance = distance;
            }
        }
        // Not into a fight nothing of ours covers: the frame is not worth
        // the construction unit. The commander counts, because it is going
        // out to take this very fight -- it only does when it can -- and
        // stands between the raiders and the frame while it does.
        if (helper && profile.builderSafety && assessExposure(sim, aiOwner, bb, builderSafetyParams(profile), frame.position, std::nullopt).exposed)
        {
            helper.reset();
        }
        if (helper)
        {
            LOG_INFO << "AI player " << aiOwner.value << ": the commander hands its " << frame.unitType << " frame " << frameId->value << " to unit " << helper->value;
            sim.eventLog.event(sim.gameTime.value, "army_commander_hands")
                .set("player", aiOwner.value)
                .set("unit", bb.commanderUnitId ? bb.commanderUnitId->value : 0u)
                .set("frame", frameId->value)
                .set("subject", frame.unitType)
                .set("helper", helper->value)
                .set("why", "handover")
                .detail("the commander hands its frame to a construction unit");
            outCommands.push_back(PlayerUnitCommand(*helper, PlayerUnitCommand::IssueOrder(RepairOrder(*frameId), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
        }
        return false;
    }

    void ArmyManager::updateCommanderSafety(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands) const
    {
        bb.commanderThreat.reset();
        bb.commanderFleeing = false;
        if (profile.commanderDangerRadius <= 0_ss || !bb.commanderUnitId)
        {
            bb.commanderInDanger = false;
            bb.commanderEngagedTarget.reset();
            return;
        }
        auto commanderRef = sim.tryGetUnitState(*bb.commanderUnitId);
        if (!commanderRef || commanderRef->get().isDead())
        {
            bb.commanderInDanger = false;
            bb.commanderEngagedTarget.reset();
            return;
        }
        const auto& commander = commanderRef->get();
        const auto& commanderDef = sim.unitDefinitions.at(commander.unitType);

        // Two causes: it has lost hit points since the last pass, or there
        // is something armed beside it. Either keeps the alarm up for ten
        // seconds, so a lull between salvoes does not send it back to work.
        bool hurt = bb.commanderLastHitPoints > 0 && commander.hitPoints < bb.commanderLastHitPoints;
        bb.commanderLastHitPoints = commander.hitPoints;

        auto radiusSquared = profile.commanderDangerRadius * profile.commanderDangerRadius;
        SimScalar nearest = 0_ss;
        int threats = 0;
        // What the threats cost to build, summed in id order, which is the
        // measure of whether the commander can take them on.
        float threatMetal = 0.0f;
        // And the nearest of them standing on land, which is the only kind
        // it is sent at: a ship off the shore is a threat to it, but walked
        // after, the commander ends up on the seabed where it cannot fire --
        // the reason commanderAnswersHarassment is off by default.
        std::optional<UnitId> nearestOnLand;
        SimScalar nearestOnLandDistance = 0_ss;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            // Not aircraft: there is no running from a bomber, and the
            // answer to one is the anti-air the commander is about to build.
            if (!enemy.isArmed || enemy.isAir || !inSightRecently(bb, profile, enemy))
            {
                continue;
            }
            auto contact = contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy);
            if (!contact || (contact->unit && isNanoframe(sim, *contact->unit)))
            {
                continue;
            }
            auto d = commander.position.distanceSquared(enemy.lastKnownPosition);
            if (d > radiusSquared)
            {
                continue;
            }
            ++threats;
            threatMetal += sim.unitDefinitions.at(enemy.unitType).buildCostMetal.value;
            const auto& enemyPosition = enemy.lastKnownPosition;
            if (sim.terrain.getHeightAt(enemyPosition.x, enemyPosition.z) >= sim.terrain.getSeaLevel()
                && (!nearestOnLand || d < nearestOnLandDistance))
            {
                nearestOnLand = enemy.unitId;
                nearestOnLandDistance = d;
            }
            if (!bb.commanderThreat || d < nearest)
            {
                nearest = d;
                bb.commanderThreat = enemy.unitId;
            }
        }
        if (hurt || bb.commanderThreat)
        {
            bb.commanderDangerUntil = GameTime(bb.now.value + (10u * SimTicksPerSecond));
        }
        bool was = bb.commanderInDanger;
        bb.commanderInDanger = bb.now.value < bb.commanderDangerUntil.value;
        if (bb.commanderInDanger && !was)
        {
            LOG_INFO << "AI player " << aiOwner.value << ": the commander is in danger (" << (hurt ? "taking damage" : "armed enemy close")
                     << ") at " << static_cast<int>(commander.position.x.value) << "," << static_cast<int>(commander.position.z.value);
            sim.eventLog.event(sim.gameTime.value, "army_commander_danger")
                .set("player", aiOwner.value)
                .set("unit", bb.commanderUnitId ? bb.commanderUnitId->value : 0u)
                .set("x", static_cast<int>(commander.position.x.value))
                .set("z", static_cast<int>(commander.position.z.value))
                .set("why", hurt ? "taking_damage" : "enemy_close")
                .detail("the commander is in danger");
        }
        // A chase this rule started that has run on past where the fight
        // was: called off, and the commander back in the planner's hands.
        // Raiders are faster than a commander, and one walked after them is
        // one walked out of its base.
        if (bb.commanderEngagedTarget)
        {
            auto target = sim.tryGetUnitState(*bb.commanderEngagedTarget);
            bool onIt = isAttackingUnit(commander, *bb.commanderEngagedTarget) || isDgunning(commander, *bb.commanderEngagedTarget);
            auto chase = profile.commanderDangerRadius + (profile.commanderDangerRadius / 2_ss);
            if (!onIt || !target || target->get().isDead())
            {
                bb.commanderEngagedTarget.reset();
            }
            else if (commander.position.distanceSquared(target->get().position) > chase * chase)
            {
                // Back to the frame it left, if that still stands; otherwise
                // stop where it is.
                auto frame = bb.commanderReturnFrame ? sim.tryGetUnitState(*bb.commanderReturnFrame) : std::nullopt;
                if (frame && !frame->get().isDead() && isNanoframe(sim, frame->get()))
                {
                    outCommands.push_back(PlayerUnitCommand(*bb.commanderUnitId, PlayerUnitCommand::IssueOrder(RepairOrder(*bb.commanderReturnFrame), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                }
                else
                {
                    outCommands.push_back(moveCommand(*bb.commanderUnitId, commander.position));
                }
                bb.commanderEngagedTarget.reset();
                bb.commanderReturnFrame.reset();
            }
        }

        if (!bb.commanderInDanger)
        {
            return;
        }

        auto maxHitPoints = commanderDef.maxHitPoints;
        bool canFight;
        if (profile.commanderStandsItsGround)
        {
            // It runs from a fight it is losing, from more than it can take
            // on, and from what it cannot fire at where it stands.
            bool low = static_cast<long long>(commander.hitPoints) * 100 < static_cast<long long>(maxHitPoints) * profile.commanderRetreatBelowPercent;
            canFight = canFireFrom(sim, commander) && !low && threatMetal <= static_cast<float>(profile.commanderFightsUpToMetal);
        }
        else
        {
            // A lone raider it can shoot is already answered further down, and a
            // commander is the best gun the base owns: it runs only from what it
            // cannot fight -- more than it may take on alone, something it
            // cannot fire at from where it stands, or a fight it is losing.
            bool losing = commander.hitPoints * 2u < maxHitPoints;
            canFight = canFireFrom(sim, commander)
                && std::max(threats, static_cast<int>(bb.enemiesNearBase.size())) <= std::max(1, profile.commanderDefendsAloneMaxIntruders) && !losing;
        }
        if (canFight)
        {
            if (profile.commanderStandsItsGround && nearestOnLand)
            {
                // A frame in hand is finished first when the army can take
                // the fight and the frame would not keep.
                bool engaged = bb.commanderEngagedTarget.has_value();
                if (!engaged && profile.commanderKeepsFrames && commanderStaysOnFrame(sim, aiOwner, profile, bb, commander, threatMetal, outCommands))
                {
                    return;
                }
                // The frame it goes from is where it comes back to once the
                // fight is done, queued behind the fight.
                std::optional<UnitId> returnTo;
                if (!engaged && profile.commanderKeepsFrames && bb.commanderReturnFrame && frameInHand(sim, commander) == bb.commanderReturnFrame)
                {
                    returnTo = bb.commanderReturnFrame;
                }
                auto queueReturn = [&]() {
                    if (returnTo)
                    {
                        outCommands.push_back(PlayerUnitCommand(*bb.commanderUnitId, PlayerUnitCommand::IssueOrder(RepairOrder(*returnTo), PlayerUnitCommand::IssueOrder::IssueKind::Queued)));
                    }
                };
                // The D-gun first, when there is something in its reach.
                if (profile.commanderUsesDgun)
                {
                    if (auto shot = chooseDgunTarget(sim, aiOwner, profile, bb, *bb.commanderUnitId, commander, commanderDef))
                    {
                        if (!isDgunning(commander, *shot))
                        {
                            LOG_INFO << "AI player " << aiOwner.value << ": the commander D-guns " << shot->value;
                            sim.eventLog.event(sim.gameTime.value, "army_commander_dgun")
                                .set("player", aiOwner.value)
                                .set("unit", bb.commanderUnitId ? bb.commanderUnitId->value : 0u)
                                .set("target_id", shot->value)
                                .set("why", "dgun")
                                .detail("the commander D-guns an enemy");
                            outCommands.push_back(PlayerUnitCommand(*bb.commanderUnitId, PlayerUnitCommand::IssueOrder(DgunOrder(*shot), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                            queueReturn();
                        }
                        bb.commanderEngagedTarget = *shot;
                        return;
                    }
                }
                // Otherwise the nearest of them, unless a D-gun order is
                // still walking into reach of another.
                bool dgunning = !commander.orders.empty() && std::holds_alternative<DgunOrder>(commander.orders.front());
                if (!dgunning && !isAttackingUnit(commander, *nearestOnLand))
                {
                    outCommands.push_back(attackCommand(*bb.commanderUnitId, *nearestOnLand));
                    queueReturn();
                    bb.commanderEngagedTarget = *nearestOnLand;
                }
            }
            return;
        }
        bb.commanderEngagedTarget.reset();

        // Where to: home if it is away from home, since home is where the
        // towers and the army are; and if it is already there, straight away
        // from whatever is shooting, toward the rally point failing that.
        std::optional<SimVector> refuge;
        if (bb.baseAnchor && commander.position.distanceSquared(*bb.baseAnchor) > (300_ss * 300_ss))
        {
            refuge = *bb.baseAnchor;
        }
        else if (bb.commanderThreat)
        {
            auto threat = bb.knownEnemies.find(bb.commanderThreat->value);
            if (threat != bb.knownEnemies.end())
            {
                auto away = (commander.position - threat->second.lastKnownPosition).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
                refuge = commander.position + (away * 350_ss);
                // Never out of sight: straight away from a threat on the
                // near side of a corner start is straight off the visible
                // map, which is where one commander in a replay ran to and
                // died.
                refuge = clampInsideVisibleMap(sim.terrain, *refuge, 64_ss);
            }
        }
        else if (bb.rallyPoint)
        {
            refuge = *bb.rallyPoint;
        }
        if (refuge)
        {
            bb.commanderFleeing = true;
            if (!isMovingTo(commander, *refuge))
            {
                outCommands.push_back(moveCommand(*bb.commanderUnitId, *refuge));
            }
        }
    }

    std::optional<SimVector> ArmyManager::navalRallyPoint(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const SimVector& navalHome) const
    {
        if (profile.navalRallyDistance <= 0_ss)
        {
            return std::nullopt;
        }
        if (navalRallyMemo && navalRallyMemo->home.distanceSquared(navalHome) == 0_ss)
        {
            return navalRallyMemo->station;
        }

        // Sixteen bearings, and the first that is deep water on our own sea
        // with deep water all the way out to it. The walk out matters: a
        // point across a spit from the yard passes the water-body test --
        // that test is about connectivity and not about this line -- and a
        // hull sent to it goes the long way round the spit, which is the
        // opposite of standing by.
        static const float bearings[16][2] = {
            {1.0f, 0.0f}, {0.9239f, 0.3827f}, {0.7071f, 0.7071f}, {0.3827f, 0.9239f},
            {0.0f, 1.0f}, {-0.3827f, 0.9239f}, {-0.7071f, 0.7071f}, {-0.9239f, 0.3827f},
            {-1.0f, 0.0f}, {-0.9239f, -0.3827f}, {-0.7071f, -0.7071f}, {-0.3827f, -0.9239f},
            {0.0f, -1.0f}, {0.3827f, -0.9239f}, {0.7071f, -0.7071f}, {0.9239f, -0.3827f}};

        auto seaLevel = sim.terrain.getSeaLevel();
        auto deepAt = [&](const SimVector& p, SimScalar depth) {
            auto ground = sim.terrain.tryGetHeightAt(p.x, p.z);
            return ground && *ground <= seaLevel - depth;
        };

        NavalRallyMemo memo{navalHome, std::nullopt};
        for (const auto& b : bearings)
        {
            SimVector candidate(
                navalHome.x + (profile.navalRallyDistance * SimScalar(b[0])),
                seaLevel,
                navalHome.z + (profile.navalRallyDistance * SimScalar(b[1])));
            if (!deepAt(candidate, 20_ss) || !sameWaterBody(bb.mapIntel, sim.terrain, navalHome, candidate))
            {
                continue;
            }
            bool clearRun = true;
            for (int step = 1; step <= 4 && clearRun; ++step)
            {
                auto t = SimScalar(static_cast<float>(step) / 5.0f);
                SimVector sample(
                    navalHome.x + ((candidate.x - navalHome.x) * t),
                    seaLevel,
                    navalHome.z + ((candidate.z - navalHome.z) * t));
                clearRun = deepAt(sample, 12_ss);
            }
            if (clearRun)
            {
                memo.station = candidate;
                break;
            }
        }
        navalRallyMemo = memo;
        return memo.station;
    }

    void ArmyManager::updateNavy(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands) const
    {
        if (bb.navalCombatUnits.empty())
        {
            return;
        }

        // Home is our own shipyard, not the (land) base anchor or rally
        // point -- a ship called back to either of those is exactly the
        // "sent inland" bug this exists to avoid. No shipyard standing
        // means no home to hold, and with nothing to measure a leash
        // against a fleet is left where it is rather than guessed at; that
        // should not arise in practice, since nothing builds a hull without
        // a shipyard to build it.
        std::optional<SimVector> navalHome;
        if (!bb.sideUnits.shipyard.empty())
        {
            for (const auto& [_, unit] : sim.units)
            {
                if (unit.owner == aiOwner && unit.isAlive() && unit.unitType == bb.sideUnits.shipyard)
                {
                    navalHome = unit.position;
                    break;
                }
            }
        }
        if (!navalHome)
        {
            return;
        }
        auto navalStation = navalRallyPoint(sim, profile, bb, *navalHome);

        // Whether there is a fleet, as opposed to a ship or two. The hull on
        // loan to ScoutManager is not part of it -- it is already busy, and
        // counting it would send the rest off a ship short.
        auto fleetSize = static_cast<int>(bb.navalCombatUnits.size()) - (bb.navalScoutUnitId ? 1 : 0);

        // Gathered is not the same as owned. A fleet of six with four of them
        // already dead in the enemy's harbour is two ships at home, and
        // sending those two after the four is how a side spends a whole game
        // feeding the other one hull at a time. So: count what is actually
        // at the yard, sail when that is a fleet, and let later hulls follow
        // only in groups.
        auto gatherRadiusSquared = (profile.rallyDistance * 2_ss) * (profile.rallyDistance * 2_ss);
        int gatheredAtHome = 0;
        for (auto shipId : bb.navalCombatUnits)
        {
            if (bb.navalScoutUnitId && *bb.navalScoutUnitId == shipId)
            {
                continue;
            }
            auto shipRef = sim.tryGetUnitState(shipId);
            if (shipRef && shipRef->get().position.distanceSquared(*navalHome) <= gatherRadiusSquared)
            {
                ++gatheredAtHome;
            }
        }
        // Capped at the sail threshold: a floor of 2 alone inverts the two thresholds at fleet size 1, so recall (2) sits above sail (1) and the state flips every pass.
        auto reinforcementSize = std::min(profile.navalAttackFleetSize, std::max(2, profile.navalAttackFleetSize / 2));
        if (profile.navalAttackFleetSize <= 0)
        {
            bb.navalSortieActive = false;
        }
        else if (!bb.navalSortieActive && gatheredAtHome >= profile.navalAttackFleetSize)
        {
            bb.navalSortieActive = true;
            LOG_INFO << "AI player " << aiOwner.value << " navy: " << gatheredAtHome << " hulls gathered, the fleet sails";
            sim.eventLog.event(sim.gameTime.value, "navy_sail")
                .set("player", aiOwner.value)
                .set("hulls", gatheredAtHome)
                .set("why", "fleet_gathered")
                .detail("the fleet sails");
        }
        else if (bb.navalSortieActive && fleetSize < reinforcementSize)
        {
            bb.navalSortieActive = false;
            LOG_INFO << "AI player " << aiOwner.value << " navy: " << fleetSize << " hull(s) left, the fleet is recalled";
            sim.eventLog.event(sim.gameTime.value, "navy_recall")
                .set("player", aiOwner.value)
                .set("hulls", fleetSize)
                .set("why", "fleet_depleted")
                .detail("the fleet is recalled");
        }
        auto fleetReady = bb.navalSortieActive;
        // Hulls still at the yard join a fleet that is out only as a group.
        auto homeGroupSails = fleetReady && gatheredAtHome >= reinforcementSize;

        // An upper bound on any distance on this map, so "the nearest one
        // anywhere" costs no knob of its own. It is only ever handed to
        // nearestNavalEnemy, whose sameWaterBody test is what keeps the
        // search honest: it will not name a target the hull cannot swim to,
        // so widening the radius cannot send a fleet at an inland base.
        auto wholeMap = sim.terrain.getWidthInWorldUnits() + sim.terrain.getHeightInWorldUnits();

        // Where the fleet is going, if it is going anywhere: the nearest
        // enemy on our own water, REMEMBERED rather than currently seen.
        //
        // nearestNavalEnemy insists on inSightRecently, and that is right for
        // choosing what to shoot -- targetMemoryTicks is five seconds, and a
        // weapon should not fire at a memory. It is wrong for choosing where
        // to sail. Asking the shooting question of a going decision is what
        // the first version of this did, and it never once left harbour:
        // measured on Brain Coral the AI sits at "known enemies 0" for
        // forty-three of sixty-two samples and never sees more than one, so
        // the tuned and control arms came out byte-identical over ten games.
        //
        // sameWaterBody still applies, so a remembered target on the wrong
        // sea is no more sailed at than a visible one would be.
        std::optional<SimVector> fleetObjective;
        if (fleetReady && bb.mapIntel.valid)
        {
            auto bestDistanceSquared = wholeMap * wholeMap;
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                if (!sameWaterBody(bb.mapIntel, sim.terrain, *navalHome, enemy.lastKnownPosition))
                {
                    continue;
                }
                // Nearest to THEIR base once that is known, not to ours. The
                // nearest thing to our own yard is whatever wandered closest,
                // and a fleet sent after that spends the game drifting from
                // one stray hull to the next and never arrives anywhere; what
                // is in the way gets shot by the engage rule regardless.
                const auto& measureFrom = bb.enemyBasePosition ? *bb.enemyBasePosition : *navalHome;
                auto d = measureFrom.distanceSquared(enemy.lastKnownPosition);
                if (d <= bestDistanceSquared)
                {
                    bestDistanceSquared = d;
                    fleetObjective = enemy.lastKnownPosition;
                }
            }

            // Nothing remembered on our sea: sail for the deep water nearest
            // their base -- or, before that is found, nearest the start
            // position furthest from us -- rather than sit at home with a
            // fleet and a full store, which is how one two-hour game ended.
            // A shipyard site is used because it is known to be water a hull
            // can reach and lie in, and theirs is likely to be beside it.
            if (!fleetObjective && bb.baseAnchor)
            {
                std::optional<SimVector> towards = bb.enemyBasePosition;
                if (!towards)
                {
                    // Every start position but our own, taken in turn for two
                    // minutes each, furthest first. It was the furthest one
                    // alone, and on a map that declares ten starts and deals
                    // the seats at random that is usually an empty corner: in
                    // one ninety-minute game a fleet of thirty-nine hulls
                    // sailed to it and lay there, with the enemy never found.
                    std::vector<std::pair<SimScalar, SimVector>> candidates;
                    for (const auto& start : bb.mapIntel.startPositions)
                    {
                        auto d = bb.baseAnchor->distanceSquared(start);
                        if (d > (profile.defendRadius * profile.defendRadius))
                        {
                            candidates.emplace_back(d, start);
                        }
                    }
                    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                    if (!candidates.empty())
                    {
                        auto leg = static_cast<std::size_t>(bb.now.value / (120u * SimTicksPerSecond)) % candidates.size();
                        towards = candidates[leg].second;
                    }
                }
                if (towards)
                {
                    // Asked once per destination, not once per pass. The
                    // site list is every heightmap cell a shipyard fits on,
                    // which on an open sea is most of the map: profiled on
                    // Crystal Isles this scan alone was 6 ms a pass and
                    // four-fifths of everything the AI cost in the game.
                    // The answer depends on nothing but the two places, so
                    // remembering it cannot change it.
                    if (!navalWaypointMemo || navalWaypointMemo->towards != *towards || navalWaypointMemo->home != *navalHome)
                    {
                        NavalWaypointMemo memo{*towards, *navalHome, std::nullopt};
                        auto best = wholeMap * wholeMap;
                        for (const auto& site : bb.mapIntel.shipyardSites)
                        {
                            if (!sameWaterBody(bb.mapIntel, sim.terrain, *navalHome, site.position))
                            {
                                continue;
                            }
                            auto d = towards->distanceSquared(site.position);
                            if (d < best)
                            {
                                best = d;
                                memo.site = site.position;
                            }
                        }
                        navalWaypointMemo = memo;
                    }
                    fleetObjective = navalWaypointMemo->site;
                }
            }
        }

        // Their commander, if it is somewhere a torpedo can reach. It walks
        // the seabed, where nothing but a waterweapon can touch it, and a
        // submarine has nothing but a waterweapon: the one hull built for
        // exactly the target that ends the game.
        std::optional<KnownEnemy> enemyCommander;
        if (fleetReady && bb.mapIntel.valid)
        {
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                auto defIt = sim.unitDefinitions.find(enemy.unitType);
                if (defIt != sim.unitDefinitions.end() && defIt->second.commander
                    && sameWaterBody(bb.mapIntel, sim.terrain, *navalHome, enemy.lastKnownPosition))
                {
                    enemyCommander = enemy;
                    break;
                }
            }
        }

        for (auto shipId : bb.navalCombatUnits)
        {
            auto shipRef = sim.tryGetUnitState(shipId);
            if (!shipRef)
            {
                continue;
            }
            // The hull ScoutManager borrowed is not ours to order. The land
            // side guards its own stand-in scout this way in six places; the
            // naval side had no such guard, so without this both managers
            // would command the same ship on the same tick, each undoing the
            // other's order.
            if (bb.navalScoutUnitId && *bb.navalScoutUnitId == shipId)
            {
                continue;
            }
            const auto& ship = shipRef->get();
            const bool atHome = ship.position.distanceSquared(*navalHome) <= gatherRadiusSquared;
            const bool sails = fleetReady && (!atHome || homeGroupSails);

            // On its way to somewhere it can hit from; see NavalTargetProgress.
            if (auto moving = navalRepositioning.find(shipId.value); moving != navalRepositioning.end())
            {
                bool arrived = ship.position.distanceSquared(moving->second.first) <= (64_ss * 64_ss);
                if (arrived || bb.now.value >= moving->second.second.value)
                {
                    navalRepositioning.erase(moving);
                }
                else
                {
                    if (!isMovingTo(ship, moving->second.first))
                    {
                        outCommands.push_back(moveCommand(shipId, moving->second.first));
                    }
                    continue;
                }
            }

            // The commander comes first. A hull near enough goes for what is
            // shooting it, or to it if that is not known.
            if (bb.commanderInDanger && bb.commanderUnitId)
            {
                auto commanderRef = sim.tryGetUnitState(*bb.commanderUnitId);
                if (commanderRef && ship.position.distanceSquared(commanderRef->get().position) <= (profile.commanderGuardRadius * profile.commanderGuardRadius)
                    && sameWaterBody(bb.mapIntel, sim.terrain, ship.position, commanderRef->get().position))
                {
                    if (bb.commanderThreat)
                    {
                        if (!isAttackingUnit(ship, *bb.commanderThreat))
                        {
                            outCommands.push_back(attackCommand(shipId, *bb.commanderThreat));
                        }
                    }
                    else if (!isMovingTo(ship, commanderRef->get().position))
                    {
                        outCommands.push_back(moveCommand(shipId, commanderRef->get().position));
                    }
                    continue;
                }
            }

            if (sails && enemyCommander && !bb.sideUnits.submarine.empty() && ship.unitType == bb.sideUnits.submarine)
            {
                auto commanderRef = sim.tryGetUnitState(enemyCommander->unitId);
                if (commanderRef && !commanderRef->get().isDead() && inSightRecently(bb, profile, *enemyCommander))
                {
                    if (!isAttackingUnit(ship, enemyCommander->unitId))
                    {
                        outCommands.push_back(attackCommand(shipId, enemyCommander->unitId));
                    }
                }
                else if (!isMovingTo(ship, enemyCommander->lastKnownPosition))
                {
                    outCommands.push_back(moveCommand(shipId, enemyCommander->lastKnownPosition));
                }
                continue;
            }

            // Anything on our own sea gets shot at, whatever else is
            // happening -- the land army's own "anything within reach"
            // rule, just asked with nearestNavalEnemy instead.
            if (auto enemy = nearestNavalEnemy(sim, aiOwner, profile, bb, ship.position, profile.engageRadius))
            {
                // Is it getting hurt? If nothing we have thrown at it in
                // navalStalledAttackSeconds has moved its hit points, the
                // shots are not arriving -- a torpedo stopped by a shelf, a
                // shell by a cliff -- and firing on from the same place will
                // not change that. Move round it to deep water with deep
                // water all the way in, a different side each attempt.
                //
                // Not asked only of a hull that still holds the attack order:
                // the simulation drops an order it cannot carry out, and a
                // hull being handed the same target every pass is exactly the
                // one that is getting nowhere.
                auto targetRef = sim.tryGetUnitState(*enemy);
                if (profile.navalStalledAttackSeconds > 0 && targetRef)
                {
                    const auto& target = targetRef->get();
                    auto& progress = navalTargetProgress[enemy->value];
                    if (progress.since.value == 0 || target.hitPoints != progress.hitPoints)
                    {
                        progress.hitPoints = target.hitPoints;
                        progress.since = bb.now;
                    }
                    else if (bb.now.value - progress.since.value > static_cast<unsigned int>(profile.navalStalledAttackSeconds) * SimTicksPerSecond)
                    {
                        ++progress.attempt;
                        progress.since = bb.now;
                    }

                    auto& answered = navalAttemptAnswered[std::make_pair(shipId.value, enemy->value)];
                    if (progress.attempt > answered)
                    {
                        answered = progress.attempt;
                        // Eight bearings round the target, starting somewhere
                        // different for each attempt and each ship.
                        static const float bearings[8][2] = {
                            {1.0f, 0.0f}, {0.7071f, 0.7071f}, {0.0f, 1.0f}, {-0.7071f, 0.7071f},
                            {-1.0f, 0.0f}, {-0.7071f, -0.7071f}, {0.0f, -1.0f}, {0.7071f, -0.7071f}};
                        auto seaLevel = sim.terrain.getSeaLevel();
                        auto deepAt = [&](const SimVector& p, SimScalar depth) {
                            auto ground = sim.terrain.tryGetHeightAt(p.x, p.z);
                            return ground && *ground <= seaLevel - depth;
                        };
                        std::optional<SimVector> spot;
                        for (int k = 0; k < 8 && !spot; ++k)
                        {
                            const auto& b = bearings[(progress.attempt * 3 + static_cast<int>(shipId.value % 8u) + k) % 8];
                            for (float range : {240.0f, 340.0f, 160.0f})
                            {
                                SimVector candidate(target.position.x + SimScalar(b[0] * range), seaLevel, target.position.z + SimScalar(b[1] * range));
                                if (!deepAt(candidate, 25_ss) || !sameWaterBody(bb.mapIntel, sim.terrain, ship.position, candidate))
                                {
                                    continue;
                                }
                                // Deep water all the way in: four samples
                                // along the torpedo's run.
                                bool clearRun = true;
                                for (int step = 1; step <= 4 && clearRun; ++step)
                                {
                                    auto t = SimScalar(static_cast<float>(step) / 5.0f);
                                    SimVector sample(candidate.x + ((target.position.x - candidate.x) * t), seaLevel, candidate.z + ((target.position.z - candidate.z) * t));
                                    clearRun = deepAt(sample, 12_ss);
                                }
                                if (clearRun)
                                {
                                    spot = candidate;
                                    break;
                                }
                            }
                        }
                        if (spot)
                        {
                            LOG_DEBUG << "AI navy: ship " << shipId.value << " is not hurting " << target.unitType << " " << enemy->value
                                      << "; moving round to " << static_cast<int>(spot->x.value) << "," << static_cast<int>(spot->z.value) << " (attempt " << progress.attempt << ")";
                            sim.eventLog.event(sim.gameTime.value, "navy_reposition")
                                .set("player", aiOwner.value)
                                .set("unit", shipId.value)
                                .set("target", target.unitType)
                                .set("target_id", enemy->value)
                                .set("x", static_cast<double>(spot->x.value))
                                .set("z", static_cast<double>(spot->z.value))
                                .set("attempt", progress.attempt)
                                .set("why", "stalled_attack")
                                .detail("ship moves round a target it is not hurting");
                            navalRepositioning[shipId.value] = std::make_pair(*spot, GameTime(bb.now.value + (20u * SimTicksPerSecond)));
                            outCommands.push_back(moveCommand(shipId, *spot));
                            continue;
                        }
                    }
                }

                // Outranging it: lie off at the edge of our own reach, where
                // it cannot answer, and fire from there next pass
                // (navalStandOff).
                if (auto standOff = hullStandOff(sim, profile, bb, ship, sim.unitDefinitions.at(ship.unitType), *enemy))
                {
                    if (!isMovingTo(ship, *standOff))
                    {
                        sim.eventLog.event(sim.gameTime.value, "navy_reposition")
                            .set("player", aiOwner.value)
                            .set("unit", shipId.value)
                            .set("target_id", enemy->value)
                            .set("x", static_cast<double>(standOff->x.value))
                            .set("z", static_cast<double>(standOff->z.value))
                            .set("why", "stand_off")
                            .detail("ship lies off at the edge of its own reach");
                        outCommands.push_back(moveCommand(shipId, *standOff));
                    }
                    continue;
                }
                if (!isAttackingUnit(ship, *enemy))
                {
                    outCommands.push_back(attackCommand(shipId, *enemy));
                }
                continue;
            }

            // With a fleet gathered, go and find them rather than waiting to
            // be visited. The test above is bounded by engageRadius and so
            // only ever answers what is already in front of us; this is the
            // same question asked across our own sea.
            //
            // Not gated on the land army's Attack phase on purpose. That
            // phase turns on attackArmySize, which counts units that walk,
            // and on an all-water map it is never reached -- so a fleet
            // waiting for it would be waiting on an army that cannot exist.
            if (fleetObjective && sails)
            {
                // Sail at it rather than attack-order it: the memory is a
                // place, and the unit that was there may be gone or unseen.
                // The engage test above is what opens fire, once something
                // is actually in front of us.
                if (!isMovingTo(ship, *fleetObjective))
                {
                    outCommands.push_back(moveCommand(shipId, *fleetObjective));
                }
                continue;
            }

            // Nothing worth fighting: hold the coast at home instead of
            // drifting, so the fleet is already where the next thing worth
            // shooting turns up.
            //
            // At the STATION rather than at the yard. A hull that has just
            // been built is handed a BuggerOffOrder, which takes it one
            // footprint off the pad and no further, and both tests below used
            // to measure against navalHome -- which IS the shipyard -- so a
            // hull at the doors was already where it was supposed to be and
            // was never told to move again. Two hulls waiting on a third
            // therefore parked across the mouth of the yard building it, and
            // a spawn point a friendly hull is standing on is ten failed
            // tries and a lost queue entry (GameSimulation::retryBlockedSite)
            // for as long as it stands there. Reported from play: "boats were
            // blocking the factory after being made", and "core shipyard
            // stopped building as it got blocked by a scout ship".
            //
            // Distances are still measured from the yard. The station is
            // navalRallyDistance from it and gatherRadius is twice
            // rallyDistance, so a hull standing by still counts as gathered.
            const auto& station = navalStation ? *navalStation : *navalHome;
            // A hull still out after the recall comes back whatever it was
            // doing; one that is merely idle comes back when it has drifted
            // OFF STATION. Drift used to be measured from the yard, which is
            // what made a hull on the pad read as already in place: it is the
            // one spot the old test could never object to. Measured from the
            // station instead, the yard is rallyDistance-and-more away from
            // where the hull should be, so sitting on it is drift like any
            // other -- and a hull actually at the station is at zero and is
            // never ordered anywhere on the next pass.
            bool recalled = !fleetReady && !atHome && !isMovingTo(ship, station);
            bool drifted = ship.position.distanceSquared(station) > (profile.rallyDistance * profile.rallyDistance)
                && ship.orders.empty();
            if (recalled || drifted)
            {
                outCommands.push_back(moveCommand(shipId, station));
            }
        }
    }

    void ArmyManager::answerHarassmentWithCommander(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands) const
    {
        if (!profile.commanderAnswersHarassment || bb.besiegedFactories.empty() || !bb.commanderUnitId)
        {
            return;
        }

        // Only when there is genuinely nothing else. The commander is the
        // base's entire build capacity and the game's loss condition, so it
        // goes at a raider as a last resort rather than as a tactic -- and a
        // production site under siege is exactly the case where "last
        // resort" and "usual state of affairs" coincide, because everything
        // that would otherwise answer is dying as a frame before it can
        // move.
        if (!bb.combatUnits.empty() || !bb.navalCombatUnits.empty())
        {
            return;
        }

        auto commanderRef = sim.tryGetUnitState(*bb.commanderUnitId);
        if (!commanderRef || commanderRef->get().isDead())
        {
            return;
        }
        const auto& commander = commanderRef->get();
        if (!commander.orders.empty())
        {
            // Busy. Building something is worth more than a shot at a scout,
            // and a commander already attacking needs no second order.
            return;
        }

        // BuildManager runs earlier in the same tick and may have just given
        // the commander a job; the order has not reached its queue yet -- a
        // PlayerCommand takes at least a tick to land -- so the queue above
        // cannot see it. This flag is what does.
        if (bb.commanderTasked)
        {
            return;
        }

        // Leashed to the same radius any other unit of ours picks a fight
        // at. The commander does not cross the map for this: if the gun is
        // further off than that, walking to it is a base left unbuilt, and
        // on the map this is for it is as likely to be water the commander
        // cannot cross as ground it can.
        auto enemy = nearestKnownEnemy(sim, aiOwner, profile, bb, commander.position, profile.engageRadius);
        if (!enemy)
        {
            return;
        }
        LOG_INFO << "AI army: the commander answers the siege of a production site, attacking " << enemy->value;
        sim.eventLog.event(sim.gameTime.value, "army_defend")
            .set("player", commander.owner.value)
            .set("unit", bb.commanderUnitId->value)
            .set("target_id", enemy->value)
            .set("factories", bb.besiegedFactories.size())
            .set("why", "commander_answers_siege")
            .detail("the commander answers the siege of a production site");
        outCommands.push_back(attackCommand(*bb.commanderUnitId, *enemy));
    }

    std::optional<UnitId> ArmyManager::chooseRaidTarget(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const ThreatMap& threatMap) const
    {
        // A building of theirs that is not beside their base and has nothing
        // covering it; of those, the nearest to us, because a raid that walks
        // the length of the map is a raid that arrives to find a tower.
        //
        // This is the answer to a side mining a whole flank uncontested. The
        // wave goes at whatever value the threat map picks out, which is
        // nearly always the base, so nothing was ever sent at the extractors
        // and the enemy had no reason to keep anything at home.
        if (!bb.baseAnchor)
        {
            return std::nullopt;
        }
        std::optional<UnitId> best;
        SimScalar bestDistanceSquared = 0_ss;
        auto avoidSquared = profile.raidAvoidBaseRadius * profile.raidAvoidBaseRadius;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (!enemy.isBuilding)
            {
                continue;
            }
            if (!contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy))
            {
                continue;
            }
            // Of the enemy we are fighting, where there is one. The rule
            // below keeps a raid clear of their base, and with three
            // opponents that leaves ANOTHER opponent's base as a legal
            // target -- far from the one we are avoiding, and often
            // undefended, so it is exactly what the nearest-first pick
            // would choose. Sending the raiding party to open a second war
            // is the opposite of concentrating.
            if (bb.focusEnemy && enemy.owner != *bb.focusEnemy)
            {
                continue;
            }
            if (bb.enemyBasePosition && bb.enemyBasePosition->distanceSquared(enemy.lastKnownPosition) < avoidSquared)
            {
                continue;
            }
            if (threatMap.antiGroundAt(enemy.lastKnownPosition) > 0.0f)
            {
                continue;
            }
            auto d = bb.baseAnchor->distanceSquared(enemy.lastKnownPosition);
            if (!best || d < bestDistanceSquared)
            {
                bestDistanceSquared = d;
                best = enemy.unitId;
            }
        }
        return best;
    }

    void ArmyManager::update(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const ThreatMap& threatMap,
        AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        updateRallyPoint(profile, bb);

        ++ticksSinceLastUpdate;
        if (ticksSinceLastUpdate < profile.tacticalTickInterval)
        {
            return;
        }
        ticksSinceLastUpdate = 0;

        // Decide where the army is going this pass.
        //
        // The objective is their commander, and failing that their base.
        // Killing the commander ends the game and taking the base wins it;
        // everything else the wave might walk at is a detour, and the one
        // that kept being chosen -- the highest value cell the threat map
        // could find anywhere -- is a detour the raiding parties are already
        // out making.
        bb.attackTarget.reset();
        if (bb.phase == GamePhase::Attack)
        {
            if (profile.huntEnemyCommander)
            {
                bb.attackTarget = bb.enemyCommanderPosition;
            }
            if (!bb.attackTarget)
            {
                bb.attackTarget = threatMap.bestAttackTarget(
                    profile.threatAversion.value,
                    bb.enemyBasePosition,
                    profile.attackBaseRadius);
            }
            if (!bb.attackTarget && bb.enemyBasePosition)
            {
                bb.attackTarget = bb.enemyBasePosition;
            }
            if (!bb.attackTarget && !bb.knownEnemies.empty())
            {
                bb.attackTarget = bb.knownEnemies.begin()->second.lastKnownPosition;
            }
        }

        updateCommanderSafety(sim, aiOwner, profile, bb, outCommands);
        updateAntiAir(sim, aiOwner, profile, bb, outCommands);
        updateNavy(sim, aiOwner, profile, bb, outCommands);

        // A guard for a builder placing something away from the base --
        // an outpost tower is the usual case. Modelled on the raid
        // detachment below: drawn from the reserve and released the same
        // way, except a guard stands rather than walks into a fight it
        // chose, so it is worth sending under strength where a raid of one
        // was called a gift.
        //
        // Released -- the request cleared and the group sent back to the
        // reserve -- once the builder is no longer there to protect: it
        // died, the thing it was building now stands finished, or the
        // request has simply stood too long. Checked before the request is
        // acted on further this pass, so a stale guard does not linger a
        // tick longer than it has to.
        if (bb.buildSiteGuardRequest)
        {
            const auto& request = *bb.buildSiteGuardRequest;
            auto builderRef = sim.tryGetUnitState(request.builderId);
            bool builderGone = !builderRef || builderRef->get().isDead();
            bool jobDone = somethingFinishedStandsAt(sim, aiOwner, request.position);
            bool timedOut = profile.buildSiteGuardTimeoutSeconds > 0
                && bb.now.value > request.requestedAt.value + static_cast<unsigned int>(profile.buildSiteGuardTimeoutSeconds) * SimTicksPerSecond;
            if (builderGone || jobDone || timedOut)
            {
                if (!bb.guardGroup.empty())
                {
                    LOG_INFO << "AI army: guard of " << bb.guardGroup.size() << " released, builder " << request.builderId.value
                             << (builderGone ? " lost" : (jobDone ? " finished" : " timed out"));
                    sim.eventLog.event(sim.gameTime.value, "army_guard_released")
                        .set("player", aiOwner.value)
                        .set("unit", request.builderId.value)
                        .set("guard", bb.guardGroup.size())
                        .set("why", builderGone ? "builder_gone" : (jobDone ? "finished" : "timed_out"))
                        .detail("guard released, builder " + std::string(builderGone ? "lost" : (jobDone ? "finished" : "timed out")));
                }
                bb.buildSiteGuardRequest.reset();
                bb.guardGroup.clear();
            }
        }
        else
        {
            bb.guardGroup.clear();
        }
        if (bb.buildSiteGuardRequest)
        {
            // Prune the dead out of a guard already standing, the same way
            // the raid group below is pruned.
            for (auto it = bb.guardGroup.begin(); it != bb.guardGroup.end();)
            {
                auto alive = std::binary_search(bb.combatUnits.begin(), bb.combatUnits.end(), UnitId(*it), [](UnitId a, UnitId b) { return a.value < b.value; });
                it = alive ? std::next(it) : bb.guardGroup.erase(it);
            }
            if (profile.buildSiteGuardSize > 0 && static_cast<int>(bb.guardGroup.size()) < profile.buildSiteGuardSize)
            {
                for (auto unitId : bb.combatUnits)
                {
                    if (static_cast<int>(bb.guardGroup.size()) >= profile.buildSiteGuardSize)
                    {
                        break;
                    }
                    if (bb.scoutUnitId && *bb.scoutUnitId == unitId)
                    {
                        continue;
                    }
                    if (bb.attackGroup.count(unitId.value) != 0 || bb.raidGroup.count(unitId.value) != 0 || bb.guardGroup.count(unitId.value) != 0)
                    {
                        continue;
                    }
                    bb.guardGroup.insert(unitId.value);
                }
            }
        }

        // A detachment for the enemy's outlying economy, drawn from the units
        // gathering for the next wave and never from the wave that is out.
        // Not while the base itself is under attack: the reserve has a job
        // then, and it is at home.
        bb.raidTarget.reset();
        if (profile.raidingParties && profile.raidPartySize > 0 && bb.phase == GamePhase::Attack && bb.enemiesNearBase.empty())
        {
            for (auto it = bb.raidGroup.begin(); it != bb.raidGroup.end();)
            {
                auto alive = std::binary_search(bb.combatUnits.begin(), bb.combatUnits.end(), UnitId(*it), [](UnitId a, UnitId b) { return a.value < b.value; });
                it = alive ? std::next(it) : bb.raidGroup.erase(it);
            }
            if (auto target = chooseRaidTarget(sim, aiOwner, profile, bb, threatMap))
            {
                auto known = bb.knownEnemies.find(target->value);
                if (known != bb.knownEnemies.end())
                {
                    bb.raidTarget = known->second.lastKnownPosition;
                }
                // Only once a wave is already out. Formed before that, the
                // raid comes out of the first attack the AI ever makes and
                // attackArmySize quietly means three fewer than it says; the
                // raiders are meant to be the surplus that gathers behind a
                // wave, not a piece of it.
                if (bb.raidGroup.empty() && !bb.attackGroup.empty())
                {
                    for (auto unitId : bb.combatUnits)
                    {
                        if (static_cast<int>(bb.raidGroup.size()) >= profile.raidPartySize)
                        {
                            break;
                        }
                        if (bb.scoutUnitId && *bb.scoutUnitId == unitId)
                        {
                            continue;
                        }
                        if (bb.attackGroup.count(unitId.value) != 0 || bb.guardGroup.count(unitId.value) != 0)
                        {
                            continue;
                        }
                        bb.raidGroup.insert(unitId.value);
                    }
                    if (static_cast<int>(bb.raidGroup.size()) < profile.raidPartySize)
                    {
                        // Not enough to spare. A raid of one is a gift.
                        bb.raidGroup.clear();
                    }
                    else if (bb.raidTarget)
                    {
                        LOG_INFO << "AI army: raid of " << bb.raidGroup.size() << " sent at unit " << target->value
                                 << " at " << static_cast<int>(bb.raidTarget->x.value) << "," << static_cast<int>(bb.raidTarget->z.value)
                                 << ", wave of " << bb.attackGroup.size() << " carries on";
                        sim.eventLog.event(sim.gameTime.value, "army_dispatch")
                            .set("player", aiOwner.value)
                            .set("units", bb.raidGroup.size())
                            .set("target_id", target->value)
                            .set("x", static_cast<double>(bb.raidTarget->x.value))
                            .set("z", static_cast<double>(bb.raidTarget->z.value))
                            .set("wave", bb.attackGroup.size())
                            .set("why", "raid")
                            .detail("raid sent at an outlying enemy building");
                    }
                }
            }
            else
            {
                bb.raidGroup.clear();
            }
        }
        else
        {
            bb.raidGroup.clear();
        }

        // Who is in the wave. Formed when the attack is called, from everyone
        // then in the army; pruned as they die; reinforced in batches while
        // it is still out; and declared spent once it has shrunk to the
        // retreat size. A unit built after the call does not walk to the
        // front by itself -- that is what the army used to do, and measured,
        // a side that had lost its wave sent each new kbot out alone and
        // stayed at an army of one to four for the rest of the game -- but
        // nor does it wait for the whole wave to die, which is what it used
        // to do instead.
        if (bb.phase == GamePhase::Attack && profile.attackInWaves)
        {
            for (auto it = bb.attackGroup.begin(); it != bb.attackGroup.end();)
            {
                auto alive = std::binary_search(bb.combatUnits.begin(), bb.combatUnits.end(), UnitId(*it), [](UnitId a, UnitId b) { return a.value < b.value; });
                it = alive ? std::next(it) : bb.attackGroup.erase(it);
            }
            if (bb.attackGroup.empty() && !bb.waveSpent)
            {
                for (auto unitId : bb.combatUnits)
                {
                    if (!(bb.scoutUnitId && *bb.scoutUnitId == unitId) && bb.raidGroup.count(unitId.value) == 0
                        && bb.guardGroup.count(unitId.value) == 0)
                    {
                        bb.attackGroup.insert(unitId.value);
                    }
                }
            }
            // Reinforcements, in batches. The wave is only over when it has
            // fallen below the retreat size, so without this every unit built
            // during an attack stood at the rally point until the wave that
            // set out had been ground down -- a side with a full reserve at
            // home while a handful died at the front. A batch rather than
            // each unit as it appears, because the dribble of one kbot at a
            // time walking to the front alone is what attackInWaves exists to
            // stop.
            if (!bb.attackGroup.empty() && !bb.waveSpent && profile.reinforcementGroupSize > 0)
            {
                std::vector<UnitId> reserve;
                for (auto unitId : bb.combatUnits)
                {
                    if (bb.scoutUnitId && *bb.scoutUnitId == unitId)
                    {
                        continue;
                    }
                    if (bb.attackGroup.count(unitId.value) != 0 || bb.raidGroup.count(unitId.value) != 0
                        || bb.guardGroup.count(unitId.value) != 0)
                    {
                        continue;
                    }
                    reserve.push_back(unitId);
                }
                if (static_cast<int>(reserve.size()) >= profile.reinforcementGroupSize)
                {
                    for (auto unitId : reserve)
                    {
                        bb.attackGroup.insert(unitId.value);
                    }
                    LOG_INFO << "AI army: " << reserve.size() << " reinforcements join the wave, now " << bb.attackGroup.size();
                    sim.eventLog.event(sim.gameTime.value, "army_reinforce")
                        .set("player", aiOwner.value)
                        .set("units", reserve.size())
                        .set("army", bb.attackGroup.size())
                        .set("why", "wave")
                        .detail("reinforcements join the wave");
                }
            }
            if (static_cast<int>(bb.attackGroup.size()) < profile.retreatArmySize)
            {
                bb.waveSpent = true;
            }
        }
        else
        {
            bb.attackGroup.clear();
            bb.waveSpent = false;
        }

        // Outnumbered at home: hold at the rally point and fight what comes
        // within reach, rather than sending two kbots at nine.
        auto outnumbered = profile.holdWhenOutnumbered && bb.enemiesNearBase.size() > bb.combatUnits.size();

        // The intruder to answer: of the armed enemies inside the defend
        // radius, the one nearest home. Not the nearest known enemy of any
        // kind from the base -- that used to be the rule, and with a base
        // near the map's middle it was as likely to be their solar
        // collector as the raider at our extractors.
        std::optional<UnitId> intruder;
        if (bb.baseAnchor)
        {
            SimScalar nearest = 0_ss;
            for (auto enemyId : bb.enemiesNearBase)
            {
                auto known = bb.knownEnemies.find(enemyId.value);
                if (known == bb.knownEnemies.end())
                {
                    continue;
                }
                if (!inSightRecently(bb, profile, known->second))
                {
                    continue;
                }
                // A frame is no intruder: it cannot fire until it is
                // finished, and isNanoframe says why it is no target either.
                // The builder putting it up is armed or it is not, and is
                // judged on its own entry in this list.
                auto contact = contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, known->second);
                if (!contact || (contact->unit && isNanoframe(sim, *contact->unit)))
                {
                    continue;
                }
                auto distance = bb.baseAnchor->distanceSquared(known->second.lastKnownPosition);
                if (!intruder || distance < nearest)
                {
                    nearest = distance;
                    intruder = enemyId;
                }
            }
        }

        // Raiders at a building of ours out beyond the base, and the reserve
        // near enough and strong enough to answer them (answerOutpostRaids).
        // Only when nothing is at home: the base comes first.
        std::optional<UnitId> outpostRaider;
        std::set<unsigned int> outpostResponders;
        if (profile.answerOutpostRaids && bb.baseAnchor && !intruder)
        {
            const auto baseRadiusSquared = profile.defendRadius * profile.defendRadius;
            const auto raidSquared = profile.outpostRaidRadius * profile.outpostRaidRadius;
            std::optional<SimVector> site;
            for (const auto& [_, standing] : bb.standingBuildings)
            {
                if (standing.position.distanceSquared(*bb.baseAnchor) <= baseRadiusSquared)
                {
                    continue;
                }
                for (const auto& [__, enemy] : bb.knownEnemies)
                {
                    if (!enemy.isArmed || enemy.isAir || enemy.isBuilding || !inSightRecently(bb, profile, enemy))
                    {
                        continue;
                    }
                    if (enemy.lastKnownPosition.distanceSquared(standing.position) <= raidSquared)
                    {
                        if (contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy))
                        {
                            site = standing.position;
                            outpostRaider = enemy.unitId;
                            break;
                        }
                    }
                }
                if (site)
                {
                    break;
                }
            }
            if (site)
            {
                // What the raiders there are worth, and what of the reserve
                // could get there.
                float raiders = 0.0f;
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (enemy.isArmed && !enemy.isAir && !enemy.isBuilding && inSightRecently(bb, profile, enemy)
                        && enemy.lastKnownPosition.distanceSquared(*site) <= raidSquared)
                    {
                        if (auto defIt = sim.unitDefinitions.find(enemy.unitType); defIt != sim.unitDefinitions.end())
                        {
                            raiders += defIt->second.buildCostMetal.value;
                        }
                    }
                }
                const auto responseSquared = profile.outpostResponseRadius * profile.outpostResponseRadius;
                float responders = 0.0f;
                for (auto id : bb.combatUnits)
                {
                    if ((bb.scoutUnitId && *bb.scoutUnitId == id) || bb.attackGroup.count(id.value) != 0 || bb.raidGroup.count(id.value) != 0
                        || bb.guardGroup.count(id.value) != 0)
                    {
                        continue;
                    }
                    const auto& unit = sim.getUnitState(id);
                    if (unit.position.distanceSquared(*site) > responseSquared)
                    {
                        continue;
                    }
                    outpostResponders.insert(id.value);
                    responders += sim.unitDefinitions.at(unit.unitType).buildCostMetal.value;
                }
                if (outpostResponders.empty() || responders < raiders * profile.outpostResponseStrength)
                {
                    outpostResponders.clear();
                    outpostRaider.reset();
                }
                else if (bb.outpostRaidAnswered != outpostRaider)
                {
                    LOG_INFO << "AI army: " << outpostResponders.size() << " answer raiders (" << static_cast<int>(raiders) << " metal) at "
                             << static_cast<int>(site->x.value) << "," << static_cast<int>(site->z.value);
                    sim.eventLog.event(sim.gameTime.value, "army_defend")
                        .set("player", aiOwner.value)
                        .set("units", outpostResponders.size())
                        .set("target_id", outpostRaider ? outpostRaider->value : 0u)
                        .set("x", static_cast<double>(site->x.value))
                        .set("z", static_cast<double>(site->z.value))
                        .set("raiders_metal", raiders)
                        .set("responders_metal", responders)
                        .set("why", "outpost_raid")
                        .detail("reserve answers raiders at an outpost");
                }
            }
        }
        bb.outpostRaidAnswered = outpostRaider;

        // Where the wave is, taken as one thing. Every member is handed the
        // same destination and paths to it alone, so without this the wave is
        // a column sorted by speed and the enemy meets it three at a time.
        std::optional<SimVector> waveCentre;
        {
            float sumX = 0.0f;
            float sumZ = 0.0f;
            int counted = 0;
            for (auto id : bb.combatUnits)
            {
                if (bb.attackGroup.count(id.value) == 0)
                {
                    continue;
                }
                const auto& p = sim.getUnitState(id).position;
                sumX += p.x.value;
                sumZ += p.z.value;
                ++counted;
            }
            if (counted > 0)
            {
                auto divisor = static_cast<float>(counted);
                waveCentre = SimVector(SimScalar(sumX / divisor), 0_ss, SimScalar(sumZ / divisor));
            }
        }

        // What the wave walks at. Normally the place the threat map picked,
        // but an army standing in front of us is the thing to fight: both
        // sides pick the other's base and set off, and two waves that pass
        // each other on the road trade bases instead of meeting.
        auto waveObjective = bb.attackTarget;
        if (waveCentre && profile.waveMeetEnemyCount > 0)
        {
            float sumX = 0.0f;
            float sumZ = 0.0f;
            int counted = 0;
            auto radiusSquared = profile.waveMeetEnemyRadius * profile.waveMeetEnemyRadius;
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                if (enemy.isBuilding || enemy.isAir || !enemy.isArmed)
                {
                    continue;
                }
                if (!contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy))
                {
                    continue;
                }
                if (waveCentre->distanceSquared(enemy.lastKnownPosition) >= radiusSquared)
                {
                    continue;
                }
                sumX += enemy.lastKnownPosition.x.value;
                sumZ += enemy.lastKnownPosition.z.value;
                ++counted;
            }
            if (counted >= profile.waveMeetEnemyCount)
            {
                auto divisor = static_cast<float>(counted);
                waveObjective = SimVector(SimScalar(sumX / divisor), 0_ss, SimScalar(sumZ / divisor));
            }
        }

        answerHarassmentWithCommander(sim, aiOwner, profile, bb, outCommands);

        // Nothing else can answer, so the commander answers itself.
        //
        // This sits outside the loop below on purpose: that loop walks
        // combatUnits, and the case this exists for is the one where
        // combatUnits is empty. See commanderDefendsAloneMaxIntruders for the
        // measurements, and for why the outnumbered guard used below cannot
        // be reused here.
        if (profile.commanderDefendsAloneMaxIntruders > 0
            && bb.phase == GamePhase::Defend
            && bb.combatUnits.empty()
            && intruder
            && bb.commanderUnitId
            && !bb.commanderFleeing
            && static_cast<int>(bb.enemiesNearBase.size()) <= profile.commanderDefendsAloneMaxIntruders)
        {
            // Only if it could actually hurt the intruder from where it
            // stands. The first version of this did not ask, and on an
            // all-water map it was worse than useless: an attack order at a
            // ship the commander cannot hit walks it along the seabed after
            // the ship, taking the only builder off the base to chase
            // something it will never damage.
            auto commanderRef = sim.tryGetUnitState(*bb.commanderUnitId);
            if (commanderRef && canFireFrom(sim, commanderRef->get()))
            {
                if (!isAttackingUnit(commanderRef->get(), *intruder))
                {
                    outCommands.push_back(attackCommand(*bb.commanderUnitId, *intruder));
                }
            }
        }

        // Is there anybody at home to do the mending? Asked once for the
        // pass, not once per unit: it walks every unit we own.
        //
        // Nothing in TA repairs itself, so a hurt unit sent back to a base
        // with no construction unit in it stands at the anchor for
        // mendWaitSeconds and walks out again no better than it left -- the
        // whole round trip spent, and the wave a unit short for all of it
        // (mendNeedsMender).
        //
        // A live mobile builder is the whole test. Not whether one is idle,
        // and not mendDamagedUnits: a builder busy now is free later, and
        // the commander mends as well as anything else does. What it rules
        // out is the case there is no answer to -- nobody left who could.
        bool menderAvailable = !profile.mendNeedsMender;
        if (!menderAvailable)
        {
            for (const auto& [otherId, other] : sim.units)
            {
                if (other.owner != aiOwner || !other.isAlive())
                {
                    continue;
                }
                auto otherDefIt = sim.unitDefinitions.find(other.unitType);
                if (otherDefIt == sim.unitDefinitions.end() || !otherDefIt->second.builder || !otherDefIt->second.isMobile)
                {
                    continue;
                }
                if (other.isBeingBuilt(otherDefIt->second))
                {
                    continue;
                }
                menderAvailable = true;
                break;
            }
        }

        for (auto unitId : bb.combatUnits)
        {
            if (bb.scoutUnitId && *bb.scoutUnitId == unitId)
            {
                continue;
            }
            const auto& unit = sim.getUnitState(unitId);

            // Hurt: home to be mended, and no fighting on the way
            // (retreatDamagedUnits). Three things stop this becoming a
            // crowd of units standing in the base doing nothing, which is
            // what a replay showed the first version doing: the base being
            // attacked cancels it outright, a unit waits only
            // mendWaitSeconds for a mender that may never come, and those
            // waiting stand spaced around the anchor rather than on it.
            if (profile.retreatDamagedUnits && bb.baseAnchor)
            {
                const auto& def = sim.unitDefinitions.at(unit.unitType);
                auto share = def.maxHitPoints > 0 ? (unit.hitPoints * 100) / def.maxHitPoints : 100u;
                auto leaveBelow = unit.unitType == bb.sideUnits.raider ? profile.retreatRaiderBelowPercent : profile.retreatLineBelowPercent;
                auto waiting = bb.mendingUnits.find(unitId.value);
                auto mending = waiting != bb.mendingUnits.end();
                // And the walk itself has to be worth making. In a corridor
                // the way home runs back through our own army and takes long
                // enough that the wave is a unit down while it is pushing --
                // which is the shape of the complaint this answers. Past
                // mendMaxWalkHome the unit stays and fights hurt.
                auto withinWalkHome = profile.mendMaxWalkHome <= 0_ss
                    || flatDistance(unit.position, *bb.baseAnchor) <= profile.mendMaxWalkHome;
                if (!mending && static_cast<int>(share) < leaveBelow && menderAvailable && withinWalkHome)
                {
                    waiting = bb.mendingUnits.emplace(unitId.value, bb.now).first;
                    mending = true;
                }
                else if (mending && static_cast<int>(share) >= profile.rejoinAbovePercent)
                {
                    bb.mendingUnits.erase(waiting);
                    mending = false;
                }
                if (mending)
                {
                    // Waited long enough with nobody able to mend it: back to
                    // the fight. It stays in the table, which is what stops
                    // it being sent home again on the very next pass -- the
                    // entry is cleared when it is healed, above.
                    auto waitTicks = static_cast<unsigned int>(std::max(0, profile.mendWaitSeconds)) * SimTicksPerSecond;
                    if (bb.now.value > waiting->second.value + waitTicks)
                    {
                        mending = false;
                    }
                }
                // The base comes first: an intruder is answered by whatever
                // is standing there, hurt or not.
                const bool baseAttacked = intruder.has_value() || !bb.enemiesNearBase.empty();
                if (mending && !baseAttacked)
                {
                    // One of eight standing places around the anchor, by id,
                    // so a dozen hurt units do not pile onto one spot.
                    static const std::array<std::pair<float, float>, 8> Spots{{{1.0f, 0.0f}, {0.7f, 0.7f}, {0.0f, 1.0f}, {-0.7f, 0.7f}, {-1.0f, 0.0f}, {-0.7f, -0.7f}, {0.0f, -1.0f}, {0.7f, -0.7f}}};
                    // Unit ids are handed out in steps, so id % 8 would put
                    // every unit on the same spot; spread them by a hash of
                    // the id instead.
                    const auto& spot = Spots[((unitId.value * 2654435761u) >> 29) % Spots.size()];
                    auto stand = clampInsideVisibleMap(
                        sim.terrain,
                        *bb.baseAnchor + SimVector(profile.mendStandRadius * SimScalar(spot.first), 0_ss, profile.mendStandRadius * SimScalar(spot.second)),
                        64_ss);
                    if (unit.position.distanceSquared(stand) > (profile.mendHavenRadius * profile.mendHavenRadius) && !isMovingTo(unit, stand))
                    {
                        outCommands.push_back(moveCommand(unitId, stand));
                    }
                    continue;
                }
            }

            // Is this one getting hurt? If nothing anything of ours has
            // thrown at the target in stalledAttackSeconds has moved its hit
            // points, the shots are not arriving -- it is up a slope, or
            // behind something -- and standing here firing will not change
            // that. The unit drops the target, ignores it for
            // stalledAttackForgetSeconds and takes the next one; the ground
            // it is standing on is what was wrong, so somebody else's shots
            // from somewhere else are left alone.
            //
            // The order has to be taken off it as well as the target
            // forgotten: with no other enemy in reach the rules below only
            // move a unit whose queue is empty, and a unit still holding the
            // attack would go on firing for the rest of the game. A move to
            // where it already stands is the cheapest way to say stop.
            // Anything within reach gets shot at, whatever the phase.
            if (auto enemy = nearestKnownEnemy(sim, aiOwner, profile, bb, unit.position, profile.engageRadius, false, [&](UnitId candidate) { return hasGivenUpOn(unitId, candidate, bb.now); }))
            {
                // Is it getting hurt? The question is asked of the target we
                // are about to order this unit at rather than of the order it
                // is holding, because the simulation throws an attack order
                // away the moment the target goes out of sight and the AI
                // hands it straight back -- so a unit stuck on something it
                // cannot hurt often has an empty queue and a new order every
                // pass, which is the loop itself rather than a way out of it.
                // Is the ground in the way? Asked first, because it is the
                // same question the stall clock below spends fifteen seconds
                // arriving at, and it has a better answer than dropping the
                // target: walk in until the shot clears (answerBlockedShots).
                //
                // Only once the unit is inside its own reach. A unit still
                // walking to the fight is not being stopped by anything, and
                // the line from where it set out says nothing about the line
                // from where it will stand.
                if (profile.answerBlockedShots)
                {
                    auto targetRef = sim.tryGetUnitState(*enemy);
                    auto reach = longestWeaponRange(sim, sim.unitDefinitions.at(unit.unitType));
                    if (targetRef && targetRef->get().isAlive() && reach > 0_ss
                        && flatDistance(unit.position, targetRef->get().position) <= reach)
                    {
                        if (auto stand = positionForClearShot(sim, unit, targetRef->get()))
                        {
                            auto to = clampInsideVisibleMap(sim.terrain, *stand, 64_ss);
                            // The stall clock is measuring this pairing and
                            // would give up on it the moment the unit gets
                            // its shot, the hit points not having moved
                            // while it walked. Start it again from there.
                            landTargetProgress.erase(std::make_pair(unitId.value, enemy->value));
                            if (!isMovingTo(unit, to))
                            {
                                outCommands.push_back(moveCommand(unitId, to));
                            }
                            continue;
                        }
                    }
                }

                if (profile.answerStalledAttacks && profile.stalledAttackSeconds > 0)
                {
                    auto targetRef = sim.tryGetUnitState(*enemy);
                    // Only while it is close enough to be firing: a unit
                    // still walking there is not being stopped by anything,
                    // and a clock that runs during the walk gives up on
                    // every target in the game.
                    auto reach = longestWeaponRange(sim, sim.unitDefinitions.at(unit.unitType));
                    auto key = std::make_pair(unitId.value, enemy->value);
                    if (targetRef && targetRef->get().isAlive()
                        && reach > 0_ss && flatDistance(unit.position, targetRef->get().position) <= reach)
                    {
                        auto& progress = landTargetProgress[key];
                        if (progress.since.value == 0 || targetRef->get().hitPoints != progress.hitPoints)
                        {
                            progress.hitPoints = targetRef->get().hitPoints;
                            progress.since = bb.now;
                        }
                        else if (bb.now.value - progress.since.value > static_cast<unsigned int>(profile.stalledAttackSeconds) * SimTicksPerSecond)
                        {
                            // Nothing of ours has moved its hit points in all
                            // that time. Which is a diagnosis and not yet a
                            // treatment: the unit is usually in the wrong
                            // place rather than facing the wrong enemy, and
                            // "from here" was always the operative part of
                            // it. The ground may be in the way, a wall of
                            // wrecks may be, or the shot may not have the
                            // elevation for it, and all three are answered
                            // by standing somewhere else. Reported from a
                            // replay in as many words -- units "firing but
                            // failing to inflict damage should recalculate
                            // their position so theyre not firing into
                            // elevated terrain".
                            //
                            // So it moves first, stalledAttackRepositionTries
                            // times, and only then gives up on the target.
                            auto& tries = landTargetRepositions[key];
                            if (tries < profile.stalledAttackRepositionTries)
                            {
                                ++tries;
                                const auto& target = targetRef->get();
                                // Where the ground says to stand, if the
                                // ground is the problem. Otherwise a step
                                // across the line of fire and a little
                                // closer, to the other side each time: that
                                // is what clears a corner or the end of a
                                // wreck field, which no terrain test can see.
                                auto stand = positionForClearShot(sim, unit, target);
                                if (!stand)
                                {
                                    auto toTarget = target.position - unit.position;
                                    toTarget.y = 0_ss;
                                    auto forward = toTarget.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
                                    SimVector across(-forward.z, 0_ss, forward.x);
                                    if (tries % 2 == 0)
                                    {
                                        across = -across;
                                    }
                                    stand = unit.position + (across * profile.stalledAttackSidestep)
                                        + (forward * (profile.stalledAttackSidestep / 2_ss));
                                }
                                auto to = clampInsideVisibleMap(sim.terrain, *stand, 64_ss);
                                // The clock restarts, so the new position is
                                // judged on its own rather than inheriting
                                // the old one's failure.
                                progress.hitPoints = target.hitPoints;
                                progress.since = bb.now;
                                LOG_INFO << "AI army: unit " << unitId.value << " moves to get a shot at " << target.unitType
                                         << " " << enemy->value << " (attempt " << tries << " of " << profile.stalledAttackRepositionTries << ")";
                                sim.eventLog.event(sim.gameTime.value, "army_reposition")
                                    .set("player", aiOwner.value)
                                    .set("unit", unitId.value)
                                    .set("target", target.unitType)
                                    .set("target_id", enemy->value)
                                    .set("attempt", tries)
                                    .set("max_attempts", profile.stalledAttackRepositionTries)
                                    .set("why", "stalled_attack")
                                    .detail("moves to get a shot at a target nothing it fired hurt");
                                if (!isMovingTo(unit, to))
                                {
                                    outCommands.push_back(moveCommand(unitId, to));
                                }
                                continue;
                            }

                            // Moved and still nothing: it is the target that
                            // is wrong after all, and not the ground under
                            // our feet. It is left alone for
                            // stalledAttackForgetSeconds. The order has to
                            // come off as well as the target be forgotten --
                            // the rules below only move a unit whose queue is
                            // empty, so one still holding the attack would go
                            // on firing -- and a move to where it already
                            // stands is the cheapest way to say stop.
                            landTargetGivenUp[key] =
                                GameTime(bb.now.value + (static_cast<unsigned int>(std::max(0, profile.stalledAttackForgetSeconds)) * SimTicksPerSecond));
                            landTargetProgress.erase(key);
                            landTargetRepositions.erase(key);
                            LOG_INFO << "AI army: unit " << unitId.value << " gives up on " << targetRef->get().unitType
                                     << " " << enemy->value << ", nothing it fired moved its hit points from "
                                     << profile.stalledAttackRepositionTries << " position(s)";
                            sim.eventLog.event(sim.gameTime.value, "army_retreat")
                                .set("player", aiOwner.value)
                                .set("unit", unitId.value)
                                .set("target", targetRef->get().unitType)
                                .set("target_id", enemy->value)
                                .set("attempts", profile.stalledAttackRepositionTries)
                                .set("why", "stalled_attack")
                                .detail("gives up on a target nothing it fired could hurt");
                            outCommands.push_back(moveCommand(unitId, unit.position));
                            continue;
                        }
                    }
                    else
                    {
                        // Out of reach: the attempt has not begun.
                        landTargetProgress.erase(key);
                    }
                }

                // Outranging it: stand back where it cannot answer, and
                // shoot from there next pass (kiteWithLongerRange).
                if (auto standOff = kiteBackFrom(sim, profile, unit, sim.unitDefinitions.at(unit.unitType), *enemy))
                {
                    if (!isMovingTo(unit, *standOff))
                    {
                        outCommands.push_back(moveCommand(unitId, *standOff));
                    }
                    continue;
                }
                if (!isAttackingUnit(unit, *enemy))
                {
                    outCommands.push_back(attackCommand(unitId, *enemy));
                }
                continue;
            }

            // The commander comes first: what is shooting it if that is
            // known, otherwise to its side. Only units near enough to matter,
            // so a wave at the enemy's gates is not turned round for it.
            if (bb.commanderInDanger && bb.commanderUnitId)
            {
                auto commanderRef = sim.tryGetUnitState(*bb.commanderUnitId);
                if (commanderRef && unit.position.distanceSquared(commanderRef->get().position) <= (profile.commanderGuardRadius * profile.commanderGuardRadius))
                {
                    if (bb.commanderThreat)
                    {
                        if (!isAttackingUnit(unit, *bb.commanderThreat))
                        {
                            outCommands.push_back(attackCommand(unitId, *bb.commanderThreat));
                        }
                    }
                    else if (!isMovingTo(unit, commanderRef->get().position) && unit.position.distanceSquared(commanderRef->get().position) > (160_ss * 160_ss))
                    {
                        outCommands.push_back(moveCommand(unitId, commanderRef->get().position));
                    }
                    continue;
                }
            }

            // Answering a raid on an outpost.
            if (outpostRaider && outpostResponders.count(unitId.value) != 0)
            {
                if (!isAttackingUnit(unit, *outpostRaider))
                {
                    outCommands.push_back(attackCommand(unitId, *outpostRaider));
                }
                continue;
            }

            // A guard stands over the builder it was sent to protect
            // instead of anything the phase below would otherwise have it
            // do -- the builder placing an outpost does not care whether
            // the wave has been called yet. Held to a tight leash around
            // the site itself, not the builder: the builder is what moves
            // once its order finishes, and the guard's job is the ground,
            // not the escort.
            if (bb.buildSiteGuardRequest && bb.guardGroup.count(unitId.value) != 0)
            {
                const auto& site = bb.buildSiteGuardRequest->position;
                if (!isMovingTo(unit, site) && unit.position.distanceSquared(site) > (128_ss * 128_ss))
                {
                    outCommands.push_back(moveCommand(unitId, site));
                }
                continue;
            }

            switch (bb.phase)
            {
                case GamePhase::Defend:
                {
                    // Head for the intruder closest to home.
                    if (intruder && !outnumbered)
                    {
                        if (!isAttackingUnit(unit, *intruder))
                        {
                            outCommands.push_back(attackCommand(unitId, *intruder));
                        }
                        break;
                    }
                    [[fallthrough]];
                }
                case GamePhase::Attack:
                {
                    // Raiders have their own errand and do not join the wave.
                    // Anything within reach is still shot at first -- that
                    // rule is above the switch and applies to them too.
                    if (bb.raidGroup.count(unitId.value) != 0 && bb.raidTarget)
                    {
                        if (!isMovingTo(unit, *bb.raidTarget))
                        {
                            outCommands.push_back(moveCommand(unitId, *bb.raidTarget));
                        }
                        break;
                    }
                    auto inWave = !profile.attackInWaves || bb.attackGroup.count(unitId.value) != 0;
                    if (bb.phase == GamePhase::Attack && waveObjective && inWave)
                    {
                        // A unit that has outrun the wave waits where it
                        // stands until the rest close up.
                        //
                        // The first version of this walked it back to the
                        // wave's centre, and that does not converge. The
                        // centre is dragged by whoever is furthest behind, so
                        // the leaders turn round, which moves the centre
                        // forward a little, which turns them round again: the
                        // wave grinds back and forth and never arrives.
                        // Measured over sixteen games it decided one of them,
                        // against three to seven for every other build tried.
                        // Standing still shapes the wave the same way and does
                        // converge, because then the only thing moving is the
                        // rest of the wave closing the gap.
                        auto destination = *waveObjective;
                        if (waveCentre && profile.waveCohesionRadius > 0_ss)
                        {
                            auto unitToObjective = flatDistance(unit.position, *waveObjective);
                            auto centreToObjective = flatDistance(*waveCentre, *waveObjective);
                            if (unitToObjective + profile.waveCohesionRadius < centreToObjective)
                            {
                                destination = unit.position;
                            }
                        }
                        if (!isMovingTo(unit, destination))
                        {
                            outCommands.push_back(moveCommand(unitId, destination));
                        }
                        break;
                    }
                    // The reserve: gathering for the next wave, and the ones
                    // who answer an intruder while this wave is out, since
                    // the strategic pass no longer recalls the wave for one.
                    if (bb.phase == GamePhase::Attack && intruder && !outnumbered)
                    {
                        if (!isAttackingUnit(unit, *intruder))
                        {
                            outCommands.push_back(attackCommand(unitId, *intruder));
                        }
                        break;
                    }
                    [[fallthrough]];
                }
                default:
                {
                    // Radar says something is coming: form a line across its
                    // path, three fifths of the way out to the defend radius,
                    // instead of standing in a knot at the rally point until
                    // the first shell lands among the solar collectors. Each
                    // unit takes a place along the line by its own number, so
                    // they spread out rather than all claim the middle.
                    if (bb.incomingAttackFrom && bb.baseAnchor && !intruder)
                    {
                        auto towards = (*bb.incomingAttackFrom - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
                        SimVector across(towards.z, 0_ss, 0_ss - towards.x);
                        auto place = static_cast<int>(unitId.value % 7u) - 3;
                        auto post = *bb.baseAnchor + (towards * (profile.defendRadius * SimScalar(0.6f))) + (across * SimScalar(static_cast<float>(place) * 56.0f));
                        if (!isMovingTo(unit, post) && unit.position.distanceSquared(post) > (72_ss * 72_ss))
                        {
                            outCommands.push_back(moveCommand(unitId, post));
                        }
                        break;
                    }
                    // Gather at the rally point and wait.
                    if (bb.rallyPoint && unit.orders.empty() && unit.position.distanceSquared(*bb.rallyPoint) > (96_ss * 96_ss))
                    {
                        outCommands.push_back(moveCommand(unitId, *bb.rallyPoint));
                    }
                    break;
                }
            }
        }
    }
}
