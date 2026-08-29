#include "EconomyManager.h"
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

        bb.armySize = static_cast<int>(bb.combatUnits.size()) - (bb.scoutUnitId ? 1 : 0);
        if (bb.armySize < 0)
        {
            bb.armySize = 0;
        }
    }
}
