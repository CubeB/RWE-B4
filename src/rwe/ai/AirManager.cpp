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
        (void)threatMap;

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

        // What the bombers are for: the dearest thing the enemy has built,
        // nearest first among equals. A bomber reaches an extractor behind a
        // wall of towers, which is the whole reason to own one, and it is
        // what gives a side that is mining a flank uncontested a reason to
        // spend metal on cover instead.
        std::optional<UnitId> bomberTarget;
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
            auto value = defIt->second.buildCostMetal.value;
            auto distanceSquared = bb.baseAnchor ? bb.baseAnchor->distanceSquared(enemy.lastKnownPosition) : 0_ss;
            if (value > bestValue || (value == bestValue && bomberTarget && distanceSquared < bestDistanceSquared))
            {
                bestValue = value;
                bestDistanceSquared = distanceSquared;
                bomberTarget = enemy.unitId;
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
