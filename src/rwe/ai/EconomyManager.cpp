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

namespace rwe
{
    void EconomyManager::refresh(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        AiBlackboard& bb) const
    {
        (void)profile;

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
        bb.orphanedFrames.clear();

        const auto& player = sim.getPlayer(aiOwner);
        if (!bb.sideUnitsResolved)
        {
            bb.sideUnits = resolveAiSideUnits(sim, player.side);
            bb.sideUnitsResolved = true;
            // Worth a line once: a name the data does not define is cleared
            // rather than reported, so an empty one here is the difference
            // between "the AI chose not to" and "the AI could not".
            LOG_INFO << "AI side " << player.side << ": lab " << bb.sideUnits.lab
                     << ", advanced lab " << (bb.sideUnits.advancedLab.empty() ? "(none)" : bb.sideUnits.advancedLab)
                     << ", advanced constructor " << (bb.sideUnits.advancedConstructor.empty() ? "(none)" : bb.sideUnits.advancedConstructor)
                     << ", advanced assault " << (bb.sideUnits.advancedAssault.empty() ? "(none)" : bb.sideUnits.advancedAssault);
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
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive())
            {
                continue;
            }

            const auto& def = sim.unitDefinitions.at(unit.unitType);
            ++bb.ownedTotalCounts[unit.unitType];

            const bool isCompleted = !unit.isBeingBuilt(def);
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

        bb.armySize = static_cast<int>(bb.combatUnits.size()) - (bb.scoutUnitId ? 1 : 0);
        if (bb.armySize < 0)
        {
            bb.armySize = 0;
        }
    }
}
