#include "ArmyManager.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
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

        /** Distance across the ground, ignoring height: units stand on terrain and targets are flat points. */
        SimScalar flatDistance(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
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

    std::optional<UnitId> ArmyManager::chooseRaidTarget(
        const GameSimulation& sim,
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
            auto unitRef = sim.tryGetUnitState(enemy.unitId);
            if (!unitRef || unitRef->get().isDead())
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
            if (auto target = chooseRaidTarget(sim, profile, bb, threatMap))
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
                        if (bb.attackGroup.count(unitId.value) != 0)
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
                    if (!(bb.scoutUnitId && *bb.scoutUnitId == unitId) && bb.raidGroup.count(unitId.value) == 0)
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
                    if (bb.attackGroup.count(unitId.value) != 0 || bb.raidGroup.count(unitId.value) != 0)
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
                auto distance = bb.baseAnchor->distanceSquared(known->second.lastKnownPosition);
                if (!intruder || distance < nearest)
                {
                    nearest = distance;
                    intruder = enemyId;
                }
            }
        }

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
                auto enemyRef = sim.tryGetUnitState(enemy.unitId);
                if (!enemyRef || enemyRef->get().isDead())
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
