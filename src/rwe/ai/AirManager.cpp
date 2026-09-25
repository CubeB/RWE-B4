#include "AirManager.h"
#include <rwe/ai/PerceptionManager.h>
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

        /** As ArmyManager's: a contact nobody has seen lately is not a target. See AiTuningProfile::targetMemoryTicks. */
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
        if (!bb.sideUnitsResolved
            || (s.fighter.empty() && s.bomber.empty() && s.gunship.empty() && s.seaplaneFighter.empty() && s.torpedoSeaplane.empty()))
        {
            return;
        }

        // Ours, finished and alive, in id order.
        std::vector<UnitId> fighters;
        std::vector<UnitId> bombers;
        std::vector<UnitId> gunships;
        std::vector<UnitId> torpedoPlanes;
        // Where an aircraft with nothing to do waits, best first: a repair
        // pad, then the plant that built it.
        //
        // It used to wait over bb.baseAnchor, which is the centre of mass of
        // everything we own -- the middle of the base, and just as often
        // directly on top of a building. On Dark Side that was the vehicle
        // plant, and every idle Brawler sat over it: watched, that reads as
        // the vehicle plant building gunships, which is what was reported.
        //
        // The pad first because it is not only tidier. The simulation mends
        // an aircraft that lands on a pad (sim/airbase.test.cpp), so one
        // waiting there goes back out whole, and the AI has only just
        // started building them.
        std::optional<SimVector> repairPad;
        std::optional<SimVector> airPlant;
        for (const auto& [unitId, unit] : sim.units)
        {
            // A held mission unit is the mission's to fly, not the AI's.
            if (unit.owner != aiOwner || !unit.isAlive() || unit.heldByMission)
            {
                continue;
            }
            auto defIt = sim.unitDefinitions.find(unit.unitType);
            if (defIt == sim.unitDefinitions.end() || unit.isBeingBuilt(defIt->second))
            {
                continue;
            }
            if (!repairPad && !s.airRepairPad.empty() && unit.unitType == s.airRepairPad)
            {
                repairPad = unit.position;
            }
            else if (!airPlant && ((!s.advancedAirPlant.empty() && unit.unitType == s.advancedAirPlant)
                        || (!s.airPlant.empty() && unit.unitType == s.airPlant)))
            {
                airPlant = unit.position;
            }
            if ((!s.fighter.empty() && unit.unitType == s.fighter) || (!s.seaplaneFighter.empty() && unit.unitType == s.seaplaneFighter))
            {
                fighters.push_back(UnitId(unitId));
            }
            else if (!s.gunship.empty() && unit.unitType == s.gunship)
            {
                gunships.push_back(UnitId(unitId));
            }
            else if (!s.torpedoSeaplane.empty() && unit.unitType == s.torpedoSeaplane)
            {
                torpedoPlanes.push_back(UnitId(unitId));
            }
            else if (!s.bomber.empty() && unit.unitType == s.bomber)
            {
                bombers.push_back(UnitId(unitId));
            }
        }

        // Anything of the three, in that order. Everything else in this file
        // still measures FROM bb.baseAnchor -- what is at our door, how far a
        // sortie is -- because those are questions about the base and not
        // about where to stand and wait.
        auto waitPoint = repairPad ? repairPad : (airPlant ? airPlant : bb.baseAnchor);

        // Where our wave is. Taken from the attack group rather than from
        // every combat unit we own, for the reason the battlefield reclaim
        // takes it from there: the reserve stands at the rally point, and a
        // centroid dragged halfway home names a place where nothing is
        // happening. Both arms want it -- the gunships to find what the wave
        // is stuck on, the bombers to leave alone what the wave is about to
        // kill anyway.
        std::optional<SimVector> waveCentre;
        if (!bombers.empty() || !gunships.empty())
        {
            SimScalar sumX = 0_ss;
            SimScalar sumZ = 0_ss;
            int counted = 0;
            for (auto id : bb.combatUnits)
            {
                if (bb.attackGroup.count(id.value) == 0)
                {
                    continue;
                }
                auto ref = sim.tryGetUnitState(id);
                if (!ref || ref->get().isDead())
                {
                    continue;
                }
                sumX += ref->get().position.x;
                sumZ += ref->get().position.z;
                ++counted;
            }
            if (counted > 0)
            {
                auto divisor = intToSimScalar(counted);
                waveCentre = SimVector(sumX / divisor, 0_ss, sumZ / divisor);
            }
        }

        // Where the bombers go. A bombing run is a trade, and it has to be
        // worth flying: three questions, asked in order, and none of them
        // asked at all when we have no aircraft -- the last of them walks
        // the known enemies twice.
        std::optional<UnitId> bomberTarget;
        // Asked when gunships are standing too, not only bombers: it is
        // what a gunship falls back on when there is no army to help.
        if (!bombers.empty() || !gunships.empty())
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
                if (!contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy))
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

            // Two: which building of theirs is worth the sortie. Buildings
            // first because that is what a bomber is for -- it reaches the
            // extractor behind the wall of towers, which nothing else of
            // ours can.
            //
            // This used to be the dearest one under a ceiling of anti-air
            // cover, which is the shape that put every bomber the AI ever
            // built into the middle of the enemy base: the dearest thing
            // they own is the thing they have built their guns around, and a
            // cover ceiling either lets that through or refuses every target
            // on the map. Reported from a replay, in as many words -- "make
            // bombers more intelligent in their selection of target".
            //
            // So it scores rather than maximises, and the four terms are the
            // four things that decide whether a run is worth flying:
            //
            //   value  -- what it costs them, with a factory counted for
            //             more than its metal (bomberFactoryWeight), because
            //             a plant is not a loss of 1900 metal, it is the
            //             loss of everything it would have built next;
            //   cover  -- graded inside the ceiling rather than only at it
            //             (bomberCoverPenalty), so a lightly-picketed
            //             extractor beats a dearer one under three flak;
            //   reach  -- the flight is a round trip and we die at the far
            //             end of it (bomberSortieScale);
            //   ours   -- what our own army is already walking onto is worth
            //             a fraction of what it is worth to us
            //             (bomberLeaveToArmyRadius), because bombing it
            //             spends an aircraft on a thing that was going to
            //             die regardless, and the ground cannot reach what
            //             we ought to be spending the aircraft on.
            //
            // Every weight is on the profile and every one of them is a
            // guess until the arena says otherwise.
            if (!bomberTarget)
            {
                float bestScore = 0.0f;
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
                    auto defIt = sim.unitDefinitions.find(enemy.unitType);
                    if (defIt == sim.unitDefinitions.end())
                    {
                        continue;
                    }
                    auto cover = threatMap.antiAirCoverAt(enemy.lastKnownPosition);
                    if (cover > static_cast<float>(profile.bomberMaxAntiAirCover))
                    {
                        continue;
                    }

                    const auto& def = defIt->second;
                    auto score = static_cast<float>(def.buildCostMetal.value);
                    // A factory is the immobile thing that builds: killing
                    // one is worth more than the metal standing in it.
                    if (!def.isMobile && def.builder)
                    {
                        score *= std::max(1.0f, profile.bomberFactoryWeight);
                    }
                    if (cover > 0.0f && profile.bomberCoverPenalty > 0.0f)
                    {
                        score /= 1.0f + (cover * profile.bomberCoverPenalty);
                    }
                    if (bb.baseAnchor && profile.bomberSortieScale > 0_ss)
                    {
                        auto reach = bb.baseAnchor->distance(enemy.lastKnownPosition) / profile.bomberSortieScale;
                        score /= 1.0f + std::max(0.0f, reach.value);
                    }
                    if (waveCentre
                        && waveCentre->distanceSquared(enemy.lastKnownPosition)
                            <= (profile.bomberLeaveToArmyRadius * profile.bomberLeaveToArmyRadius))
                    {
                        score *= std::max(0.0f, profile.bomberArmyReachDiscount);
                    }

                    if (!bomberTarget || score > bestScore)
                    {
                        bestScore = score;
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
                    if (!inSightRecently(bb, profile, enemy))
                    {
                        continue;
                    }
                    if (!contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy))
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
                        if (!contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, other))
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
            if (waitPoint && unit.orders.empty() && unit.position.distanceSquared(*waitPoint) > (profile.fighterLeash * profile.fighterLeash))
            {
                outCommands.push_back(moveCommand(unitId, *waitPoint));
            }
        }

        // Gunships. They hold a position and keep firing, which is the one
        // thing the rest of the air arm cannot do and the one thing a ground
        // army stopped by a ridge needs done for it.
        //
        // So their first question is not the bombers' -- what is the dearest
        // thing the enemy owns -- but what is standing in front of our army.
        // A gunship is over the ridge rather than behind it, so the ground
        // that is holding the wave up is not holding the gunship up, and
        // that is the whole reason to spend the metal on one.
        if (!gunships.empty())
        {
            std::optional<UnitId> gunshipTarget;
            if (waveCentre)
            {
                // The armed ground enemy nearest the wave, under the same
                // anti-air ceiling the bombers fly under. Armed, because an
                // extractor behind the line is the bombers' business and not
                // what the wave is stuck on.
                auto reachSquared = profile.gunshipSupportRadius * profile.gunshipSupportRadius;
                SimScalar nearest = 0_ss;
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (enemy.isAir || !enemy.isArmed || !inSightRecently(bb, profile, enemy))
                    {
                        continue;
                    }
                    if (!contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy))
                    {
                        continue;
                    }
                    if (threatMap.antiAirCoverAt(enemy.lastKnownPosition) > static_cast<float>(profile.bomberMaxAntiAirCover))
                    {
                        continue;
                    }
                    auto distanceSquared = waveCentre->distanceSquared(enemy.lastKnownPosition);
                    if (distanceSquared > reachSquared)
                    {
                        continue;
                    }
                    if (!gunshipTarget || distanceSquared < nearest)
                    {
                        nearest = distanceSquared;
                        gunshipTarget = enemy.unitId;
                    }
                }
            }
            // No army to help, or nothing near it: fall in behind the
            // bombers rather than stand on the pad.
            if (!gunshipTarget)
            {
                gunshipTarget = bomberTarget;
            }

            auto gunshipsReady = static_cast<int>(gunships.size()) >= std::max(1, profile.gunshipPackSize);
            for (auto unitId : gunships)
            {
                const auto& unit = sim.getUnitState(unitId);
                if (gunshipTarget && gunshipsReady)
                {
                    if (!isAttackingUnit(unit, *gunshipTarget))
                    {
                        outCommands.push_back(attackCommand(unitId, *gunshipTarget));
                    }
                    continue;
                }
                if (waitPoint && unit.orders.empty() && unit.position.distanceSquared(*waitPoint) > (profile.fighterLeash * profile.fighterLeash))
                {
                    outCommands.push_back(moveCommand(unitId, *waitPoint));
                }
            }
        }

        // Torpedo seaplanes. A torpedo only hurts what is in the water, so
        // the bombers' question -- the dearest thing they own -- is the wrong
        // one: it names a fusion plant on a hill. Theirs is the nearest thing
        // of the enemy's that is over water and seen lately, hulls before
        // buildings, and in pairs at least for the bombers' reason.
        if (!torpedoPlanes.empty())
        {
            std::optional<UnitId> torpedoTarget;
            std::pair<bool, SimScalar> best{true, 0_ss};
            if (static_cast<int>(torpedoPlanes.size()) >= 2)
            {
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (enemy.isAir || !inSightRecently(bb, profile, enemy))
                    {
                        continue;
                    }
                    if (!contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy))
                    {
                        continue;
                    }
                    auto ground = sim.terrain.tryGetHeightAt(enemy.lastKnownPosition.x, enemy.lastKnownPosition.z);
                    if (!ground || *ground >= sim.terrain.getSeaLevel())
                    {
                        continue;
                    }
                    auto distance = bb.baseAnchor ? bb.baseAnchor->distanceSquared(enemy.lastKnownPosition) : 0_ss;
                    auto key = std::make_pair(enemy.isBuilding, distance);
                    if (!torpedoTarget || key < best)
                    {
                        best = key;
                        torpedoTarget = enemy.unitId;
                    }
                }
            }
            for (auto unitId : torpedoPlanes)
            {
                const auto& unit = sim.getUnitState(unitId);
                if (torpedoTarget)
                {
                    if (!isAttackingUnit(unit, *torpedoTarget))
                    {
                        outCommands.push_back(attackCommand(unitId, *torpedoTarget));
                    }
                    continue;
                }
                if (waitPoint && unit.orders.empty() && unit.position.distanceSquared(*waitPoint) > (profile.fighterLeash * profile.fighterLeash))
                {
                    outCommands.push_back(moveCommand(unitId, *waitPoint));
                }
            }
        }

        // Fighters take what flies, and only what flies: an aircraft sent at
        // a tower is an aircraft traded for nothing.
        for (auto unitId : fighters)
        {
            const auto& unit = sim.getUnitState(unitId);

            // What carries a bomb or a torpedo before what only looks: a
            // fighter that peels off after a scout leaves the torpedo
            // bombers to their run on the fleet.
            std::optional<UnitId> target;
            std::pair<bool, SimScalar> nearest{true, 0_ss};
            for (const auto& [_, enemy] : bb.knownEnemies)
            {
                if (!enemy.isAir)
                {
                    continue;
                }
                if (!inSightRecently(bb, profile, enemy))
                {
                    continue;
                }
                if (!contactStillStanding(sim, aiOwner, profile.cheatModeOmniscient, enemy))
                {
                    continue;
                }
                auto key = std::make_pair(!enemy.isArmed, unit.position.distanceSquared(enemy.lastKnownPosition));
                if (!target || key < nearest)
                {
                    nearest = key;
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
            if (!waitPoint)
            {
                continue;
            }
            if (unit.position.distanceSquared(*waitPoint) <= profile.fighterLeash * profile.fighterLeash)
            {
                continue;
            }
            if (!isMovingTo(unit, *waitPoint))
            {
                outCommands.push_back(moveCommand(unitId, *waitPoint));
            }
        }
    }
}
