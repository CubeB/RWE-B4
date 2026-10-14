#include "MetalMakerManager.h"

#include <algorithm>
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
            if (profile.metalMakerUseSurplus)
            {
                makersToRun.clear();
                if (bb.now.value % SimTicksPerSecond == 0)
                {
                    settledMakerDraw = 0.0f;
                }
            }
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

        if (profile.metalMakerUseSurplus)
        {
            updateFromSurplus(sim, profile, bb, outCommands);
            return;
        }

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

    void MetalMakerManager::updateFromSurplus(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        struct Converter
        {
            UnitId id;
            float energy;
            float metal;
            bool active;
        };
        std::vector<Converter> converters;
        float activeDraw = 0.0f;
        for (auto id : bb.metalMakers)
        {
            auto unit = sim.tryGetUnitState(id);
            if (!unit || unit->get().isDead())
            {
                continue;
            }
            auto definition = sim.unitDefinitions.find(unit->get().unitType);
            if (definition == sim.unitDefinitions.end() || unit->get().isBeingBuilt(definition->second))
            {
                continue;
            }
            const auto& def = definition->second;
            if (!def.onOffable || def.makesMetal.value <= 0.0f)
            {
                continue;
            }
            auto energy = std::max(0.0f, def.energyUse.value);
            converters.push_back({id, energy, def.makesMetal.value, unit->get().activated});
            if (unit->get().activated)
            {
                activeDraw += energy;
            }
        }

        // AI runs immediately before updateResources on a multiple of 30.
        // Its blackboard sees that settle's totals on the following tick.
        // Using today's active count with yesterday's demand would alternate
        // between switching everything off and switching it back on.
        if (bb.now.value % SimTicksPerSecond == 0)
        {
            settledMakerDraw = activeDraw;
        }
        if (!makersOn)
        {
            makersToRun.clear();
            overflowOn = false;
        }
        else if (bb.now.value % SimTicksPerSecond == 1)
        {
            surplusReady = true;
            const auto otherDemand = std::max(0.0f, bb.energyDemand.value - settledMakerDraw);
            auto budget = std::max(0.0f, bb.energyIncome.value - otherDemand);
            // A surplus smaller than one converter's draw must still become
            // metal eventually. Pulse one extra converter between a nearly
            // full store and the normal activation mark; sustainable ones
            // stay on. Never spend the bank to cover an underlying deficit.
            const auto reserve = bb.energyStorage.value * static_cast<float>(profile.metalMakerOnAbovePercent) / 100.0f;
            if (budget <= 0.0f || bb.currentEnergy.value <= reserve)
            {
                overflowOn = false;
            }
            else if (bb.energyStorage.value > 0.0f && bb.currentEnergy.value >= bb.energyStorage.value * 0.95f)
            {
                overflowOn = true;
            }
            // Spend on the most metal per energy first; ids break ties so a
            // changing bucket order cannot change which converters are chosen.
            std::sort(converters.begin(), converters.end(), [](const Converter& a, const Converter& b) {
                const double left = static_cast<double>(a.metal) * b.energy;
                const double right = static_cast<double>(b.metal) * a.energy;
                return left == right ? a.id.value < b.id.value : left > right;
            });
            makersToRun.clear();
            for (const auto& converter : converters)
            {
                if (converter.energy <= budget)
                {
                    budget -= converter.energy;
                    makersToRun.insert(converter.id.value);
                }
            }
            if (overflowOn)
            {
                for (const auto& converter : converters)
                {
                    if (makersToRun.count(converter.id.value) == 0)
                    {
                        makersToRun.insert(converter.id.value);
                        break;
                    }
                }
            }
        }

        if (makersOn && !surplusReady)
        {
            return;
        }

        for (const auto& converter : converters)
        {
            const bool wanted = makersToRun.count(converter.id.value) != 0;
            const auto told = lastToldAt.find(converter.id.value);
            const bool pending = told != lastToldAt.end()
                && bb.now.value < told->second.value + SimTicksPerSecond;
            const auto previous = lastToldState.find(converter.id.value);
            if (pending && previous != lastToldState.end())
            {
                // An opposite command cancels a queued switch even when the
                // current activation already matches the desired state.
                if (previous->second == wanted)
                {
                    continue;
                }
            }
            else if (converter.active == wanted)
            {
                continue;
            }
            lastToldAt[converter.id.value] = bb.now;
            lastToldState[converter.id.value] = wanted;
            outCommands.emplace_back(PlayerUnitCommand(converter.id, PlayerUnitCommand::SetOnOff{wanted}));
        }
    }
}
