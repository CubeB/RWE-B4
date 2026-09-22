#include "EconomyManager.h"
#include <algorithm>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/ai/BuildManager.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>
#include <set>
#include <variant>

namespace rwe
{
    void EconomyManager::refresh(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        AiBlackboard& bb) const
    {
        bb.now = sim.gameTime;

        // Reset per-tick state.
        bb.ownedCompletedCounts.clear();
        bb.ownedTotalCounts.clear();
        bb.idleBuilderCount = 0;
        bb.commanderUnitId.reset();
        bb.commanderPosition.reset();
        bb.baseAnchor.reset();
        bb.idleBuilders.clear();
        bb.factories.clear();
        bb.metalMakers.clear();
        bb.combatUnits.clear();
        bb.scoutUnits.clear();
        bb.antiAirUnits.clear();
        bb.transports.clear();
        bb.navalCombatUnits.clear();
        bb.orphanedFrames.clear();

        const auto& player = sim.getPlayer(aiOwner);
        if (!bb.sideUnitsResolved)
        {
            bb.sideUnits = resolveAiSideUnits(sim, player.side);
            bb.sideUnitsResolved = true;

            // Is this side's level two worth its factory? Measured against
            // the best level-one fighter it would otherwise be buying, since
            // that is what the metal would have gone on.
            auto advancedValue = BuildManager::unitCombatValuePerMetal(sim, bb.sideUnits.advancedAssault);
            auto basicValue = std::max(
                BuildManager::unitCombatValuePerMetal(sim, bb.sideUnits.raider),
                BuildManager::unitCombatValuePerMetal(sim, bb.sideUnits.rocketKbot));
            bb.advancedArmyValueRatio = basicValue > 0.0f ? advancedValue / basicValue : 0.0f;
            // Worth a line once: a name the data does not define is cleared
            // rather than reported, so an empty one here is the difference
            // between "the AI chose not to" and "the AI could not".
            LOG_INFO << "AI side " << player.side << ": lab " << bb.sideUnits.lab
                     << ", advanced lab " << (bb.sideUnits.advancedLab.empty() ? "(none)" : bb.sideUnits.advancedLab)
                     << ", advanced constructor " << (bb.sideUnits.advancedConstructor.empty() ? "(none)" : bb.sideUnits.advancedConstructor)
                     << ", advanced assault " << (bb.sideUnits.advancedAssault.empty() ? "(none)" : bb.sideUnits.advancedAssault)
                     << ", level two is worth " << bb.advancedArmyValueRatio << "x level one per metal";
            // And the air arm, for the same reason: the tier is reached
            // through the air constructor's own third page rather than
            // through the advanced lab, so "no advanced aircraft plant" can
            // mean the data does not define one, or that nothing of ours can
            // build one, and those want telling apart from a log.
            LOG_INFO << "AI side " << player.side << " air: plant " << (bb.sideUnits.airPlant.empty() ? "(none)" : bb.sideUnits.airPlant)
                     << ", constructor " << (bb.sideUnits.airConstructor.empty() ? "(none)" : bb.sideUnits.airConstructor)
                     << ", advanced plant " << (bb.sideUnits.advancedAirPlant.empty() ? "(none)" : bb.sideUnits.advancedAirPlant)
                     << ", gunship " << (bb.sideUnits.gunship.empty() ? "(none)" : bb.sideUnits.gunship)
                     << ", repair pad " << (bb.sideUnits.airRepairPad.empty() ? "(none)" : bb.sideUnits.airRepairPad)
                     << "; the advanced plant is buildable by the air constructor: "
                     << (!bb.sideUnits.advancedAirPlant.empty() && !bb.sideUnits.airConstructor.empty()
                                 && bb.buildTree.canBuild(bb.sideUnits.airConstructor, bb.sideUnits.advancedAirPlant)
                             ? "yes"
                             : "no");
        }
        bb.currentMetal = player.metal;
        bb.currentEnergy = player.energy;
        bb.metalStorage = player.maxMetal;
        bb.energyStorage = player.maxEnergy;
        bb.metalStalled = player.metalStalled;
        bb.energyStalled = player.energyStalled;
        bb.metalIncome = player.previousMetalProductionBuffer;
        bb.energyIncome = player.previousEnergyProductionBuffer;
        bb.metalDemand = player.previousDesiredMetalConsumptionBuffer;
        bb.energyDemand = player.previousDesiredEnergyConsumptionBuffer;

        // What is standing this tick, to be diffed against last tick's at the
        // end of the pass. Ordered, because the losses that come out of the
        // diff go on to steer building and must do so identically on every
        // peer.
        std::map<unsigned int, StandingBuilding> standingNow;

        // And everything that is not one: mobile units, finished or not, and
        // frames of any kind. The same diff, for the losses the building one
        // cannot see -- a production run killed as fast as it is made leaves
        // no trace in standingNow at all, because nothing in it ever
        // finished. See AiBlackboard::StandingUnit.
        std::map<unsigned int, StandingUnit> standingUnitsNow;

        // Frames on the ground, and the frames some builder of ours is
        // attending to; the difference is what has been abandoned. A builder
        // counts as attending from the moment it is ordered there, not from
        // the moment it arrives, or the frame would be handed to a second
        // builder while the first was still walking.
        std::vector<UnitId> frames;
        std::set<unsigned int> attended;

        // The nominal draw of a frame at a worker's rate: what it will ask
        // for each second until it is done, whether or not it gets it.
        float committed = 0.0f;
        auto drawOf = [&](UnitId frameId, const UnitDefinition& workerDef) {
            auto frame = sim.tryGetUnitState(frameId);
            if (!frame || frame->get().isDead())
            {
                return 0.0f;
            }
            const auto& frameDef = sim.unitDefinitions.at(frame->get().unitType);
            if (!frame->get().isBeingBuilt(frameDef))
            {
                return 0.0f;
            }
            auto estimate = BuildManager::estimateBuild(frameDef, workerDef, frame->get().buildTimeCompleted);
            return estimate.seconds > 0.0f ? estimate.metal / estimate.seconds : 0.0f;
        };

        // VectorMap iterates in id order, which keeps everything below deterministic.
        // Builders already helping at a factory, offered to the planner
        // behind the genuinely idle ones: something useful is the last thing
        // to interrupt.
        std::vector<UnitId> assistingBuilders;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive())
            {
                continue;
            }

            const auto& def = sim.unitDefinitions.at(unit.unitType);
            ++bb.ownedTotalCounts[unit.unitType];

            const bool isCompleted = !unit.isBeingBuilt(def);
            if (!isCompleted || def.isMobile)
            {
                standingUnitsNow.emplace(unitId.value, StandingUnit{unit.unitType, unit.position, !isCompleted});
            }
            if (!isCompleted)
            {
                if (!def.isMobile)
                {
                    frames.push_back(unitId);
                }
                continue;
            }
            ++bb.ownedCompletedCounts[unit.unitType];

            if (def.builder)
            {
                // A builder's draw is counted once even when both records
                // name the same frame, which they do for a build in
                // progress; a repair or an assist has only the second.
                std::optional<UnitId> drawing;
                if (unit.buildOrderUnitId)
                {
                    attended.insert(unit.buildOrderUnitId->value);
                    drawing = unit.buildOrderUnitId;
                }
                if (auto building = std::get_if<UnitBehaviorStateBuilding>(&unit.behaviourState))
                {
                    attended.insert(building->targetUnit.value);
                    if (!drawing)
                    {
                        drawing = building->targetUnit;
                    }
                }
                if (auto factory = std::get_if<FactoryBehaviorStateBuilding>(&unit.factoryState); factory && factory->targetUnit)
                {
                    drawing = factory->targetUnit->first;
                }
                if (drawing)
                {
                    committed += drawOf(*drawing, def);
                }
                if (!unit.orders.empty())
                {
                    if (auto repair = std::get_if<RepairOrder>(&unit.orders.front()))
                    {
                        attended.insert(repair->target.value);
                    }
                    else if (auto guard = std::get_if<GuardOrder>(&unit.orders.front()))
                    {
                        attended.insert(guard->target.value);
                    }
                }

                // A build order the builder is still walking to has no frame
                // yet, so nothing in sim.units stands for it, and the counts
                // above would say the thing was never asked for. It was, and
                // it counts from the moment it was ordered: the planner runs
                // whenever a builder falls idle, and judged against counts
                // that did not know, the second builder plans the same thing.
                // That is where the two radars came from -- the commander
                // was sent to a site 1200 units away and the construction
                // kbot, idle a second later, put another up beside the lab.
                // Only the front order can have a frame; anything queued
                // behind it has not been started.
                bool first = true;
                for (const auto& order : unit.orders)
                {
                    if (auto build = std::get_if<BuildOrder>(&order))
                    {
                        bool framePlaced = first && (unit.buildOrderUnitId.has_value() || std::holds_alternative<UnitBehaviorStateBuilding>(unit.behaviourState));
                        if (!framePlaced)
                        {
                            ++bb.ownedTotalCounts[build->unitType];
                        }
                    }
                    first = false;
                }
            }

            if (!def.isMobile)
            {
                standingNow.emplace(unitId.value, StandingBuilding{unit.unitType, unit.position});
            }

            if (def.onOffable && def.makesMetal.value > 0.0f)
            {
                bb.metalMakers.push_back(unitId);
            }

            if (def.commander)
            {
                bb.commanderUnitId = unitId;
                bb.commanderPosition = unit.position;
                if (!bb.homePosition)
                {
                    bb.homePosition = unit.position;
                }
                bb.baseAnchor = bb.homePosition;
            }

            // Units booked onto a transport are spoken for until they are set down again.
            const bool isFerryPassenger = bb.ferryPassengers.count(unitId.value) > 0;

            if (def.builder && !def.isMobile && !def.commander)
            {
                bb.factories.push_back(unitId);
            }
            else if (def.isMobile && def.isTransport() && !def.builder)
            {
                bb.transports.push_back(unitId);
            }
            else if (def.isMobile && !isFerryPassenger
                && ((!bb.sideUnits.destroyer.empty() && unit.unitType == bb.sideUnits.destroyer)
                    || (!bb.sideUnits.submarine.empty() && unit.unitType == bb.sideUnits.submarine)
                    || (!bb.sideUnits.cruiser.empty() && unit.unitType == bb.sideUnits.cruiser)
                    || (!bb.sideUnits.battleship.empty() && unit.unitType == bb.sideUnits.battleship)
                    || (!bb.sideUnits.antiAirShip.empty() && unit.unitType == bb.sideUnits.antiAirShip)
                    || (!bb.sideUnits.scoutShip.empty() && unit.unitType == bb.sideUnits.scoutShip)))
            {
                // Hulls. See AiBlackboard::navalCombatUnits for why these are
                // sorted here rather than falling through to the ordinary
                // scout/anti-air/combat buckets below, which is what a
                // destroyer's own weapon-bearing mobile-unit shape would
                // otherwise land it in.
                bb.navalCombatUnits.push_back(unitId);
            }
            else if (def.isMobile && (isAiScoutType(bb.sideUnits, unit.unitType) || (def.canFly && !def.canAttack && !def.builder)))
            {
                bb.scoutUnits.push_back(unitId);
            }
            else if (def.builder)
            {
                if (unit.orders.empty() && !isFerryPassenger)
                {
                    ++bb.idleBuilderCount;
                    bb.idleBuilders.push_back(unitId);
                }
                else if (!isFerryPassenger && unit.orders.size() == 1 && std::holds_alternative<GuardOrder>(unit.orders.front()))
                {
                    // Lending a hand at a factory is what a builder does when
                    // the planner had nothing for it, and nothing ever took
                    // that order off again: a guard order does not end, so
                    // every builder that once ran out of work was out of the
                    // pool for the rest of the game. That is where the
                    // advanced constructor went -- built, sent to help at the
                    // level-one factory, and never planned for again, so the
                    // tier it was bought to spend was never spent. It is
                    // available; the planner leaves it where it is if it
                    // still has nothing better.
                    assistingBuilders.push_back(unitId);
                }
            }
            else if (def.isMobile && isAiAntiAirType(bb.sideUnits, unit.unitType) && !isFerryPassenger)
            {
                // Held back from the army deliberately. Anti-air that walks
                // off with the attack is not cover, and counting it as army
                // would make the AI attack sooner for having built defences.
                bb.antiAirUnits.push_back(unitId);
            }
            else if (def.isMobile && def.canAttack && !def.canFly && (!def.weapon1.empty() || !def.weapon2.empty()) && !isFerryPassenger)
            {
                bb.combatUnits.push_back(unitId);
            }
        }

        bb.idleBuilders.insert(bb.idleBuilders.end(), assistingBuilders.begin(), assistingBuilders.end());

        for (auto frameId : frames)
        {
            if (attended.count(frameId.value) == 0)
            {
                bb.orphanedFrames.push_back(frameId);
            }
        }
        bb.metalCommitted = Metal(committed);

        // No commander? Anchor the base on the first factory, then on any unit,
        // so a decapitated AI keeps building and fighting from where it is.
        if (!bb.baseAnchor)
        {
            if (!bb.factories.empty())
            {
                bb.baseAnchor = sim.getUnitState(bb.factories.front()).position;
            }
            else
            {
                for (const auto& entry : sim.units)
                {
                    const UnitState& unit = entry.second;
                    if (unit.owner == aiOwner && unit.isAlive())
                    {
                        bb.baseAnchor = unit.position;
                        break;
                    }
                }
            }
        }

        // What went missing since last tick? Anything that was standing and
        // is not standing now was destroyed or captured; either way it is
        // gone and wants replacing. The counts alone could not tell us this
        // -- they cannot distinguish a solar collector that blew up from one
        // that was never built -- which is why the ids are kept.
        //
        // Skipped on the very first pass, when there is nothing to diff
        // against and every building would otherwise read as a fresh loss.
        if (!bb.standingBuildings.empty())
        {
            for (const auto& [unitId, standing] : bb.standingBuildings)
            {
                if (standingNow.count(unitId) != 0)
                {
                    continue;
                }
                if (bb.ownReclaimTarget && bb.ownReclaimTarget->value == unitId)
                {
                    // Taken down by us, on purpose. See ownReclaimTarget.
                    bb.ownReclaimTarget.reset();
                    continue;
                }
                bb.recentLosses.insert(bb.recentLosses.begin(), LostBuilding{standing.unitType, standing.position, bb.now});
            }
            if (bb.recentLosses.size() > MaxRememberedLosses)
            {
                bb.recentLosses.resize(MaxRememberedLosses);
            }
        }
        bb.standingBuildings = std::move(standingNow);

        // Age the memory out, so a raid stops steering the build order once
        // it has been answered.
        bb.recentLosses.erase(
            std::remove_if(
                bb.recentLosses.begin(),
                bb.recentLosses.end(),
                [&](const LostBuilding& loss) { return bb.now.value - loss.lostAt.value > LossMemoryTicks; }),
            bb.recentLosses.end());

        // The same diff again for everything that is not a standing
        // building. Skipped on the first pass for the same reason, and a
        // frame that finished is not a loss: an immobile one moves out of
        // standingUnits and into standingBuildings on the tick it goes up,
        // so both maps have to be asked before calling it gone.
        if (!bb.standingUnits.empty())
        {
            for (const auto& [unitId, standing] : bb.standingUnits)
            {
                if (standingUnitsNow.count(unitId) != 0 || bb.standingBuildings.count(unitId) != 0)
                {
                    continue;
                }
                bb.recentUnitLosses.insert(
                    bb.recentUnitLosses.begin(),
                    LostUnit{standing.unitType, standing.position, bb.now, standing.underConstruction});
            }
            if (bb.recentUnitLosses.size() > MaxRememberedUnitLosses)
            {
                bb.recentUnitLosses.resize(MaxRememberedUnitLosses);
            }
        }
        bb.standingUnits = std::move(standingUnitsNow);
        bb.recentUnitLosses.erase(
            std::remove_if(
                bb.recentUnitLosses.begin(),
                bb.recentUnitLosses.end(),
                [&](const LostUnit& loss) { return bb.now.value - loss.lostAt.value > UnitLossMemoryTicks; }),
            bb.recentUnitLosses.end());

        // Which of our factories have been losing hulls where they are born.
        // Memory only -- whether anything is still there to do it again is
        // PerceptionManager's half, since it is the pass that knows what we
        // can see.
        bb.harassedFactories.clear();
        auto harassRadiusSquared = profile.productionHarassRadius * profile.productionHarassRadius;
        for (auto factoryId : bb.factories)
        {
            const auto& factoryPosition = sim.getUnitState(factoryId).position;
            for (const auto& loss : bb.recentUnitLosses)
            {
                if (!loss.underConstruction)
                {
                    continue;
                }
                if (factoryPosition.distanceSquared(loss.position) <= harassRadiusSquared)
                {
                    bb.harassedFactories.push_back(factoryId);
                    break;
                }
            }
        }

        bb.armySize = static_cast<int>(bb.combatUnits.size()) - (bb.scoutUnitId ? 1 : 0);
        if (bb.armySize < 0)
        {
            bb.armySize = 0;
        }
    }
}
