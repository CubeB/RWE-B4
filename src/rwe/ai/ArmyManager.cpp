#include "ArmyManager.h"
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

    void ArmyManager::updateRallyPoint(const AiTuningProfile& profile, AiBlackboard& bb) const
    {
        if (!bb.baseAnchor)
        {
            bb.rallyPoint.reset();
            return;
        }
        auto towards = bb.enemyBasePosition
            ? (*bb.enemyBasePosition - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss))
            : SimVector(1_ss, 0_ss, 0_ss);
        bb.rallyPoint = *bb.baseAnchor + (towards * profile.rallyDistance);
    }

    std::optional<UnitId> ArmyManager::nearestKnownEnemy(const GameSimulation& sim, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance) const
    {
        std::optional<UnitId> best;
        auto bestDistanceSquared = maxDistance * maxDistance;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            auto unitRef = sim.tryGetUnitState(enemy.unitId);
            if (!unitRef || unitRef->get().isDead())
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

    void ArmyManager::update(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const ThreatMap& threatMap,
        AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        (void)aiOwner;
        updateRallyPoint(profile, bb);

        ++ticksSinceLastUpdate;
        if (ticksSinceLastUpdate < profile.tacticalTickInterval)
        {
            return;
        }
        ticksSinceLastUpdate = 0;

        // Decide where the army is going this pass.
        bb.attackTarget.reset();
        if (bb.phase == GamePhase::Attack)
        {
            bb.attackTarget = threatMap.bestAttackTarget(profile.threatAversion.value);
            if (!bb.attackTarget && bb.enemyBasePosition)
            {
                bb.attackTarget = bb.enemyBasePosition;
            }
            if (!bb.attackTarget && !bb.knownEnemies.empty())
            {
                bb.attackTarget = bb.knownEnemies.begin()->second.lastKnownPosition;
            }
        }

        for (auto unitId : bb.combatUnits)
        {
            if (bb.scoutUnitId && *bb.scoutUnitId == unitId)
            {
                continue;
            }
            const auto& unit = sim.getUnitState(unitId);

            // Anything within reach gets shot at, whatever the phase.
            if (auto enemy = nearestKnownEnemy(sim, bb, unit.position, profile.engageRadius))
            {
                if (!isAttackingUnit(unit, *enemy))
                {
                    outCommands.push_back(attackCommand(unitId, *enemy));
                }
                continue;
            }

            switch (bb.phase)
            {
                case GamePhase::Defend:
                {
                    // Head for the intruder closest to home.
                    if (bb.baseAnchor)
                    {
                        if (auto enemy = nearestKnownEnemy(sim, bb, *bb.baseAnchor, profile.defendRadius))
                        {
                            if (!isAttackingUnit(unit, *enemy))
                            {
                                outCommands.push_back(attackCommand(unitId, *enemy));
                            }
                            break;
                        }
                    }
                    [[fallthrough]];
                }
                case GamePhase::Attack:
                {
                    if (bb.phase == GamePhase::Attack && bb.attackTarget)
                    {
                        if (!isMovingTo(unit, *bb.attackTarget))
                        {
                            outCommands.push_back(moveCommand(unitId, *bb.attackTarget));
                        }
                        break;
                    }
                    [[fallthrough]];
                }
                default:
                {
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
