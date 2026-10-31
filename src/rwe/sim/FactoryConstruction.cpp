#include "FactoryConstruction.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimulationOwnership.h>

namespace rwe
{
    void cancelFactoryProductOnDeath(GameSimulation& sim, UnitId factoryId)
    {
        // BT-266, docs/agents/factory-death-and-briefing.md: the retained
        // reference, not construction percentage or proximity, owns cancellation.
        auto& factory = sim.getUnitState(factoryId);
        std::optional<UnitId> product;
        if (auto creating = std::get_if<FactoryBehaviorStateCreatingUnit>(&factory.factoryState))
        {
            if (auto done = std::get_if<UnitCreationStatusDone>(&creating->status))
            {
                product = done->unitId;
            }
        }
        if (auto building = std::get_if<FactoryBehaviorStateBuilding>(&factory.factoryState))
        {
            if (building->targetUnit)
            {
                product = building->targetUnit->first;
            }
        }
        factory.factoryState = FactoryBehaviorStateIdle{};
        factory.buildQueue.clear();
        if (!product)
        {
            return;
        }
        auto target = sim.tryGetUnitState(*product);
        if (!target || target->get().isDead() || !simulatesLocally(sim, target->get().owner))
        {
            return;
        }
        auto& unit = target->get();
        const auto& definition = sim.unitDefinitions.at(unit.unitType);
        // Artificial completion preserves HP and does not announce delivery.
        unit.buildTimeCompleted = definition.buildTime;
        if (definition.activateWhenBuilt)
        {
            sim.activateUnit(*product);
        }
        // Direct cause-9 damage: bypass armour, but retain victim veterancy.
        // The tier is the victim's damage-taken tier, so a unit with TADR's
        // own thresholds (#934) keeps the same fraction of its kills' worth.
        const auto tier = std::min<unsigned int>(25u, veterancyLevel(veterancyRules(definition.veterancy, sim.communityPatchMode), unit.kills));
        const auto damage = (25u - tier) * 30000u * 4u / 100u;
        if (unit.hitPoints <= damage)
        {
            sim.removeUnfinishedUnit(*product);
        }
        else
        {
            unit.hitPoints -= damage;
        }
    }
}
