#include "AirManager.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>

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

        bool isMovingTo(const UnitState& unit, const SimVector& destination)
        {
            if (unit.orders.empty())
            {
                return false;
            }
            auto move = std::get_if<MoveOrder>(&unit.orders.front());
            return move != nullptr && move->destination.distanceSquared(destination) < (64_ss * 64_ss);
        }
    }

    void AirManager::update(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const ThreatMap& threatMap,
        AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        ++ticksSinceLastUpdate;
        if (ticksSinceLastUpdate < profile.tacticalTickInterval)
        {
            return;
        }
        ticksSinceLastUpdate = 0;

        const auto& s = bb.sideUnits;
        if (!bb.sideUnitsResolved || (s.fighter.empty() && s.bomber.empty()))
        {
            return;
        }

        // Ours, finished and alive, in id order.
        std::vector<UnitId> fighters;
        std::vector<UnitId> bombers;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive())
            {
                continue;
            }
            auto defIt = sim.unitDefinitions.find(unit.unitType);
            if (defIt == sim.unitDefinitions.end() || unit.isBeingBuilt(defIt->second))
            {
                continue;
            }
            if (!s.fighter.empty() && unit.unitType == s.fighter)
            {
                fighters.push_back(UnitId(unitId));
            }
            else if (!s.bomber.empty() && unit.unitType == s.bomber)
            {
                bombers.push_back(UnitId(unitId));
            }
        }

        // Where the bombers go. A bombing run is a trade, and the dearest
        // thing the enemy owns is also the thing standing deepest inside
        // their anti-air, so flying at it trades an aircraft for a fraction
        // of a building. Three questions, asked in order, and none of them
        // asked at all when we have no bombers -- the last of them walks the
        // known enemies twice.
        std::optional<UnitId> bomberTarget;
        if (!bombers.empty())
        {
            // One: is anything of theirs at our door that can shoot? Then it
            // is bombed whatever is covering it, nearest first, because that
            // is the one about to shoot something of ours. Armed is the whole
            // test: a tower creeping in on us is armed and qualifies, an
            // extractor that happens to stand near us does not, and neither
            // does an unarmed scout wandering past.
            SimScalar nearestIntruder = 0_ss;
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                if (enemy.isAir || !enemy.isArmed || !bb.baseAnchor)
                {
                    continue;
                }
                auto enemyRef = sim.tryGetUnitState(enemy.unitId);
                if (!enemyRef || enemyRef->get().isDead())
                {
                    continue;
                }
                auto distanceSquared = bb.baseAnchor->distanceSquared(enemy.lastKnownPosition);
                if (distanceSquared > (profile.bomberHomeDefenseRadius * profile.bomberHomeDefenseRadius))
                {
                    continue;
                }
                if (!bomberTarget || distanceSquared < nearestIntruder)
                {
                    nearestIntruder = distanceSquared;
                    bomberTarget = enemy.unitId;
                }
            }

            // Two: the dearest building of theirs that is lightly covered or
            // not covered at all. Buildings first because that is what a
            // bomber is for -- it reaches the extractor behind the wall of
            // towers, which nothing else of ours can.
            if (!bomberTarget)
            {
                float bestValue = -1.0f;
                SimScalar bestDistanceSquared = 0_ss;
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (!enemy.isBuilding)
                    {
                        continue;
                    }
                    auto unitRef = sim.tryGetUnitState(enemy.unitId);
                    if (!unitRef || unitRef->get().isDead())
                    {
                        continue;
                    }
                    auto defIt = sim.unitDefinitions.find(enemy.unitType);
                    if (defIt == sim.unitDefinitions.end())
                    {
                        continue;
                    }
                    if (threatMap.antiAirCoverAt(enemy.lastKnownPosition) > static_cast<float>(profile.bomberMaxAntiAirCover))
                    {
                        continue;
                    }
                    auto value = defIt->second.buildCostMetal.value;
                    auto distanceSquared = bb.baseAnchor ? bb.baseAnchor->distanceSquared(enemy.lastKnownPosition) : 0_ss;
                    if (value > bestValue || (value == bestValue && bomberTarget && distanceSquared < bestDistanceSquared))
                    {
                        bestValue = value;
                        bestDistanceSquared = distanceSquared;
                        bomberTarget = enemy.unitId;
                    }
                }
            }

            // Three: nothing standing still worth bombing, so an army
            // instead -- but only one standing together, and only one that is
            // lightly covered. A single kbot is not worth the sortie, and a
            // massed army under its own anti-air is worth less than that.
            if (!bomberTarget)
            {
                int bestCount = profile.bomberMinClusterSize - 1;
                SimScalar bestDistanceSquared = 0_ss;
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (enemy.isBuilding || enemy.isAir)
                    {
                        continue;
                    }
                    auto unitRef = sim.tryGetUnitState(enemy.unitId);
                    if (!unitRef || unitRef->get().isDead())
                    {
                        continue;
                    }
                    if (threatMap.antiAirCoverAt(enemy.lastKnownPosition) > static_cast<float>(profile.bomberMaxAntiAirCover))
                    {
                        continue;
                    }
                    int count = 0;
                    for (const auto& [__, other] : bb.knownEnemies)
                    {
                        if (other.isBuilding || other.isAir)
                        {
                            continue;
                        }
                        auto otherRef = sim.tryGetUnitState(other.unitId);
                        if (!otherRef || otherRef->get().isDead())
                        {
                            continue;
                        }
                        if (other.lastKnownPosition.distanceSquared(enemy.lastKnownPosition) <= (profile.bomberClusterRadius * profile.bomberClusterRadius))
                        {
                            ++count;
                        }
                    }
                    auto distanceSquared = bb.baseAnchor ? bb.baseAnchor->distanceSquared(enemy.lastKnownPosition) : 0_ss;
                    if (count > bestCount || (count == bestCount && bomberTarget && distanceSquared < bestDistanceSquared))
                    {
                        bestCount = count;
                        bestDistanceSquared = distanceSquared;
                        bomberTarget = enemy.unitId;
                    }
                }
            }
        }

        // Bombers go in pairs at least. One at a time, each new bomber flies
        // off at the dearest thing the enemy owns the moment it leaves the
        // pad, which is the deepest-defended thing they own, and it is traded
        // for a fraction of a building -- the same trickle that made the army
        // send each new kbot to the front alone. Two is not a formation, but
        // it is the difference between one pass and none.
        auto bombersReady = static_cast<int>(bombers.size()) >= 2;

        for (auto unitId : bombers)
        {
            const auto& unit = sim.getUnitState(unitId);
            if (bomberTarget && bombersReady)
            {
                if (!isAttackingUnit(unit, *bomberTarget))
                {
                    outCommands.push_back(attackCommand(unitId, *bomberTarget));
                }
                continue;
            }
            // Nothing worth bombing yet, or not enough of us: wait over the
            // base rather than wander. A bomber already on its run has an
            // order and is left alone, so losing one mid-attack does not
            // recall the other.
            if (bb.baseAnchor && unit.orders.empty() && unit.position.distanceSquared(*bb.baseAnchor) > (profile.fighterLeash * profile.fighterLeash))
            {
                outCommands.push_back(moveCommand(unitId, *bb.baseAnchor));
            }
        }

        // Fighters take what flies, and only what flies: an aircraft sent at
        // a tower is an aircraft traded for nothing.
        for (auto unitId : fighters)
        {
            const auto& unit = sim.getUnitState(unitId);

            std::optional<UnitId> target;
            SimScalar nearest = 0_ss;
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                if (!enemy.isAir)
                {
                    continue;
                }
                auto enemyRef = sim.tryGetUnitState(enemy.unitId);
                if (!enemyRef || enemyRef->get().isDead())
                {
                    continue;
                }
                auto distanceSquared = unit.position.distanceSquared(enemy.lastKnownPosition);
                if (!target || distanceSquared < nearest)
                {
                    nearest = distanceSquared;
                    target = enemy.unitId;
                }
            }

            if (target)
            {
                if (!isAttackingUnit(unit, *target))
                {
                    outCommands.push_back(attackCommand(unitId, *target));
                }
                continue;
            }

            // Nothing in the air: stand over the base. Cover that has flown
            // off after the last thing it chased is not cover.
            if (!bb.baseAnchor)
            {
                continue;
            }
            if (unit.position.distanceSquared(*bb.baseAnchor) <= profile.fighterLeash * profile.fighterLeash)
            {
                continue;
            }
            if (!isMovingTo(unit, *bb.baseAnchor))
            {
                outCommands.push_back(moveCommand(unitId, *bb.baseAnchor));
            }
        }
    }
}
