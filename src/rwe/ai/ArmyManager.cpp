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
        auto towards = bb.enemyBasePosition
            ? (*bb.enemyBasePosition - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss))
            : SimVector(1_ss, 0_ss, 0_ss);
        bb.rallyPoint = *bb.baseAnchor + (towards * profile.rallyDistance);
    }

    std::optional<UnitId> ArmyManager::nearestKnownEnemy(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& from, SimScalar maxDistance, bool airOnly) const
    {
        std::optional<UnitId> best;
        auto bestDistanceSquared = maxDistance * maxDistance;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (airOnly && !enemy.isAir)
            {
                continue;
            }
            if (!inSightRecently(bb, profile, enemy))
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
            if (auto enemy = nearestKnownEnemy(sim, profile, bb, unit.position, profile.engageRadius, true))
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

            // Anything on our own sea gets shot at, whatever else is
            // happening -- the land army's own "anything within reach"
            // rule, just asked with nearestNavalEnemy instead.
            if (auto enemy = nearestNavalEnemy(sim, profile, bb, ship.position, profile.engageRadius))
            {
                if (!isAttackingUnit(ship, *enemy))
                {
                    outCommands.push_back(attackCommand(shipId, *enemy));
                }
                continue;
            }

            // Nothing worth fighting: hold the coast at home instead of
            // drifting, so the fleet is already where the next thing worth
            // shooting turns up.
            if (ship.position.distanceSquared(*navalHome) > (profile.rallyDistance * profile.rallyDistance)
                && ship.orders.empty())
            {
                outCommands.push_back(moveCommand(shipId, *navalHome));
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
            if (auto enemy = nearestKnownEnemy(sim, profile, bb, unit.position, profile.engageRadius))
            {
                if (!isAttackingUnit(unit, *enemy))
                {
                    outCommands.push_back(attackCommand(unitId, *enemy));
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
