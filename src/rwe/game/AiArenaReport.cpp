#include "AiArenaReport.h"

#include <fstream>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    AiArenaReport::AiArenaReport(unsigned int sampleIntervalTicks)
        : sampleIntervalTicks(sampleIntervalTicks == 0 ? 1 : sampleIntervalTicks)
    {
    }

    void AiArenaReport::update(const GameSimulation& sim)
    {
        if (sim.gameTime.value % sampleIntervalTicks != 0)
        {
            return;
        }
        sample(sim);
    }

    void AiArenaReport::sample(const GameSimulation& sim)
    {
        auto playerCount = getSize(sim.players);

        std::vector<Row> current;
        current.reserve(static_cast<std::size_t>(playerCount));
        for (Index i = 0; i < playerCount; ++i)
        {
            const auto& p = sim.players[i];
            Row r{};
            r.tick = sim.gameTime.value;
            r.player = static_cast<int>(i);
            r.side = p.side;
            r.status = p.status == GamePlayerStatus::Alive ? "alive" : "dead";
            r.metal = p.metal.value;
            r.energy = p.energy.value;
            r.maxMetal = p.maxMetal.value;
            r.maxEnergy = p.maxEnergy.value;
            r.metalIncome = p.previousMetalProductionBuffer.value;
            r.energyIncome = p.previousEnergyProductionBuffer.value;
            r.metalDemand = p.previousDesiredMetalConsumptionBuffer.value;
            r.energyDemand = p.previousDesiredEnergyConsumptionBuffer.value;
            current.push_back(r);
        }

        // One walk of the unit list for everybody, rather than one per player.
        for (const auto& [unitId, unit] : sim.units)
        {
            auto owner = static_cast<int>(unit.owner.value);
            if (owner < 0 || owner >= static_cast<int>(current.size()))
            {
                continue;
            }
            const auto& def = sim.unitDefinitions.at(unit.unitType);

            // Remember it whether or not it is alive right now: the point of
            // the record is to survive the unit.
            everOwned[owner][unitId.value] = !def.isMobile;

            if (!unit.isAlive())
            {
                continue;
            }

            auto& r = current[static_cast<std::size_t>(owner)];
            ++r.units;
            if (!def.isMobile)
            {
                ++r.buildings;
            }
            else if (def.builder)
            {
                ++r.builders;
            }
            else if (def.canAttack && (!def.weapon1.empty() || !def.weapon2.empty()))
            {
                ++r.army;
            }
        }

        // Losses: everything ever owned that is not standing now.
        for (auto& r : current)
        {
            auto it = everOwned.find(r.player);
            if (it == everOwned.end())
            {
                continue;
            }
            for (const auto& [rawId, wasBuilding] : it->second)
            {
                auto unitRef = sim.tryGetUnitState(UnitId(rawId));
                if (unitRef && !unitRef->get().isDead())
                {
                    continue;
                }
                if (wasBuilding)
                {
                    ++r.buildingsLost;
                }
                else
                {
                    ++r.unitsLost;
                }
            }
        }

        for (auto& r : current)
        {
            rows.push_back(std::move(r));
        }
    }

    std::string AiArenaReport::write(const std::filesystem::path& csvPath, const GameSimulation& sim)
    {
        // Take a final sample whatever the interval says, so the last row is
        // the state the game actually ended in -- unless the interval has
        // just taken one at this very tick, which would duplicate it.
        if (rows.empty() || rows.back().tick != sim.gameTime.value)
        {
            sample(sim);
        }

        std::ofstream out(csvPath);
        if (out)
        {
            out << "tick,seconds,player,side,status,metal,energy,maxMetal,maxEnergy,"
                   "metalIncome,energyIncome,metalDemand,energyDemand,"
                   "units,buildings,army,builders,unitsLost,buildingsLost\n";
            for (const auto& r : rows)
            {
                out << r.tick << ','
                    << (r.tick / static_cast<unsigned int>(SimTicksPerSecond)) << ','
                    << r.player << ','
                    << r.side << ','
                    << r.status << ','
                    << r.metal << ',' << r.energy << ','
                    << r.maxMetal << ',' << r.maxEnergy << ','
                    << r.metalIncome << ',' << r.energyIncome << ','
                    << r.metalDemand << ',' << r.energyDemand << ','
                    << r.units << ',' << r.buildings << ',' << r.army << ',' << r.builders << ','
                    << r.unitsLost << ',' << r.buildingsLost << '\n';
            }
        }
        else
        {
            LOG_ERROR << "AI arena: could not write " << csvPath.string();
        }

        // The summary line is what a batch script reads. One field per player,
        // in player order, so a run of twenty games can be reduced with grep
        // and awk and nothing else.
        std::string summary = "AI-ARENA-RESULT ticks=" + std::to_string(sim.gameTime.value)
            + " seconds=" + std::to_string(sim.gameTime.value / static_cast<unsigned int>(SimTicksPerSecond));

        auto playerCount = getSize(sim.players);
        for (Index i = 0; i < playerCount; ++i)
        {
            // The last row we recorded for this player is its final state.
            const Row* last = nullptr;
            for (const auto& r : rows)
            {
                if (r.player == static_cast<int>(i))
                {
                    last = &r;
                }
            }
            if (last == nullptr)
            {
                continue;
            }
            summary += " | p" + std::to_string(i) + "=" + last->side
                + " " + last->status
                + " units=" + std::to_string(last->units)
                + " buildings=" + std::to_string(last->buildings)
                + " army=" + std::to_string(last->army)
                + " lost=" + std::to_string(last->unitsLost + last->buildingsLost)
                + " metalIncome=" + std::to_string(static_cast<int>(last->metalIncome));
        }
        return summary;
    }
}
