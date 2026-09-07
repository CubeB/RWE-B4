#include "ArmyManager.h"
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

    std::optional<UnitId> ArmyManager::nearestKnownEnemy(const GameSimulation& sim, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance, bool airOnly) const
    {
        std::optional<UnitId> best;
        auto bestDistanceSquared = maxDistance * maxDistance;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (airOnly && !enemy.isAir)
            {
                continue;
            }
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

    void ArmyManager::updateAntiAir(
        const GameSimulation& sim,
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
            if (auto enemy = nearestKnownEnemy(sim, bb, unit.position, profile.engageRadius, true))
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

        updateAntiAir(sim, profile, bb, outCommands);

        // Who is in the wave. Formed once, when the attack is called, from
        // everyone then in the army; pruned as they die; and declared spent
        // when it has shrunk to the retreat size. Anything built after the
        // call is not in it and gathers at the rally point instead of
        // walking to the front alone, which is what the army used to do:
        // measured, a side that had lost its wave sent each new kbot out
        // by itself and stayed at an army of one to four for the rest of
        // the game.
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
                    if (!(bb.scoutUnitId && *bb.scoutUnitId == unitId))
                    {
                        bb.attackGroup.insert(unitId.value);
                    }
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
                    if (bb.baseAnchor && !outnumbered)
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
                    auto inWave = !profile.attackInWaves || bb.attackGroup.count(unitId.value) != 0;
                    if (bb.phase == GamePhase::Attack && bb.attackTarget && inWave)
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
