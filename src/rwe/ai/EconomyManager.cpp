#include "EconomyManager.h"
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

        // Reset per-tick counters.
        bb.ownedCompletedCounts.clear();
        bb.ownedTotalCounts.clear();
        bb.idleBuilderCount = 0;
        bb.commanderUnitId.reset();
        bb.commanderPosition.reset();
        bb.baseAnchor.reset();

        // Pull the resource state from the GamePlayerInfo.
        // This is read-only: addResourceDelta is the legitimate channel for
        // resource mutations (from production buildings); cheating happens
        // separately in AiPlayerController::applyResourceCheats.
        const auto& player = sim.getPlayer(aiOwner);
        bb.currentMetal = player.metal;
        bb.currentEnergy = player.energy;
        bb.metalStorage = player.maxMetal;
        bb.energyStorage = player.maxEnergy;
        bb.metalStalled = player.metalStalled;
        bb.energyStalled = player.energyStalled;

        // Iterate units in deterministic order (VectorMap iteration order is
        // deterministic by ID per src/rwe/collections/VectorMap.h).
        for (const auto& entry : sim.units)
        {
            const UnitId unitId = entry.first;
            const UnitState& unit = entry.second;
            if (unit.owner != aiOwner)
            {
                continue;
            }
            if (!unit.isAlive())
            {
                continue;
            }

            const auto& def = sim.unitDefinitions.at(unit.unitType);

            ++bb.ownedTotalCounts[unit.unitType];

            const bool isCompleted = !unit.isBeingBuilt(def);
            if (isCompleted)
            {
                ++bb.ownedCompletedCounts[unit.unitType];
            }

            if (def.commander && isCompleted)
            {
                // Phase 1 always treats the most-recently-iterated commander
                // as "the" commander. We expect at most one per AI player on
                // standard maps; if a future scenario gives multiple, the
                // last-one-wins is fine because base anchor is just an
                // origin for proximity searches.
                bb.commanderUnitId = unitId;
                bb.commanderPosition = unit.position;
                bb.baseAnchor = unit.position;
            }

            if (isCompleted && def.builder && unit.orders.empty())
            {
                ++bb.idleBuilderCount;
            }
        }

        // Fallback: if we have no commander but do have *any* unit, anchor
        // the base on it so secondary AIs without a commander
        // (e.g. test scenarios) still place buildings somewhere sensible.
        if (!bb.baseAnchor)
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
}
