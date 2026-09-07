#include "EconomyManager.h"
#include <algorithm>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>

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
        bb.combatUnits.clear();
        bb.scoutUnits.clear();
        bb.antiAirUnits.clear();
        bb.transports.clear();

        const auto& player = sim.getPlayer(aiOwner);
        if (!bb.sideUnitsResolved)
        {
            bb.sideUnits = resolveAiSideUnits(sim, player.side);
            bb.sideUnitsResolved = true;
        }
        bb.currentMetal = player.metal;
        bb.currentEnergy = player.energy;
        bb.metalStorage = player.maxMetal;
        bb.energyStorage = player.maxEnergy;
        bb.metalStalled = player.metalStalled;
        bb.energyStalled = player.energyStalled;

        // What is standing this tick, to be diffed against last tick's at the
        // end of the pass. Ordered, because the losses that come out of the
        // diff go on to steer building and must do so identically on every
        // peer.
        std::map<unsigned int, StandingBuilding> standingNow;

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
                continue;
            }
            ++bb.ownedCompletedCounts[unit.unitType];

            if (!def.isMobile)
            {
                standingNow.emplace(unitId.value, StandingBuilding{unit.unitType, unit.position});
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
