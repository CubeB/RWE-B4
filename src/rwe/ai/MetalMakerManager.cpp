#include "MetalMakerManager.h"

#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    void MetalMakerManager::update(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        if (bb.metalMakers.empty())
        {
            return;
        }

        auto share = bb.energyStorage.value > 0.0f
            ? bb.currentEnergy.value / bb.energyStorage.value
            : 0.0f;
        auto offBelow = static_cast<float>(profile.metalMakerOffBelowPercent) / 100.0f;
        auto onAbove = static_cast<float>(profile.metalMakerOnAbovePercent) / 100.0f;

        if (!bb.enemiesNearBase.empty())
        {
            // The guide's own note, and it costs one condition: under attack
            // the energy is wanted by whatever is shooting back.
            makersOn = false;
        }
        else if (bb.energyStalled || share < offBelow)
        {
            makersOn = false;
        }
        else if (share > onAbove)
        {
            makersOn = true;
        }
        // Between the two marks, whatever was decided last time stands.

        for (auto unitId : bb.metalMakers)
        {
            auto unitRef = sim.tryGetUnitState(unitId);
            if (!unitRef)
            {
                continue;
            }
            const auto& unit = unitRef->get();
            if (unit.isDead() || unit.activated == makersOn)
            {
                continue;
            }
            // Not a frame: there is nothing in one to switch.
            auto defIt = sim.unitDefinitions.find(unit.unitType);
            if (defIt == sim.unitDefinitions.end() || unit.isBeingBuilt(defIt->second))
            {
                continue;
            }
            // And not again until the last telling has had time to land.
            auto told = lastToldAt.find(unitId.value);
            if (told != lastToldAt.end() && bb.now.value < told->second.value + SimTicksPerSecond)
            {
                continue;
            }
            lastToldAt[unitId.value] = bb.now;
            outCommands.emplace_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetOnOff{makersOn}));
        }
    }
}
