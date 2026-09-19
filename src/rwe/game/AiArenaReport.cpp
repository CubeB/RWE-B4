#include "AiArenaReport.h"

#include <fstream>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    namespace
    {
        std::string categorise(const UnitDefinition& def)
        {
            if (def.commander)
            {
                return "commander";
            }
            if (!def.isMobile)
            {
                if (def.builder)
                {
                    return "factory";
                }
                if (def.extractsMetal.value > 0.0f || def.energyMake.value > 0.0f
                    || def.metalMake.value > 0.0f || def.makesMetal.value > 0.0f)
                {
                    return "economy";
                }
                if (def.canAttack)
                {
                    return "defence";
                }
                // Radar, storage, anything else that only stands there.
                return "support";
            }
            if (def.builder)
            {
                return "builder";
            }
            if (def.canFly && !def.canAttack)
            {
                return "scout";
            }
            if (def.canAttack)
            {
                return "army";
            }
            return "other";
        }
    }

    AiArenaReport::AiArenaReport(unsigned int sampleIntervalTicks)
        : sampleIntervalTicks(sampleIntervalTicks == 0 ? 1 : sampleIntervalTicks)
    {
    }

    void AiArenaReport::update(const GameSimulation& sim)
    {
        // Every tick, because when a thing was started is the question a
        // person is asking when they read the timeline.
        trackUnits(sim);

        if (sim.gameTime.value % sampleIntervalTicks == 0)
        {
            sample(sim);
        }
    }

    void AiArenaReport::trackUnits(const GameSimulation& sim)
    {
        auto now = sim.gameTime.value;

        for (const auto& [unitId, unit] : sim.units)
        {
            const auto& def = sim.unitDefinitions.at(unit.unitType);
            auto it = units.find(unitId.value);
            if (it == units.end())
            {
                UnitRecord r{};
                r.player = static_cast<int>(unit.owner.value);
                r.unitType = unit.unitType;
                r.isBuilding = !def.isMobile;
                r.category = categorise(def);
                r.bornTick = now;
                r.dead = false;
                r.completed = false;
                it = units.emplace(unitId.value, std::move(r)).first;
            }

            auto& record = it->second;
            if (!record.completed && !unit.isBeingBuilt(def))
            {
                record.completed = true;
                record.completedTick = now;
            }
            // One line per death, because the arena's own lost= figure says
            // how many and nothing at all about what. Reading a loss count
            // of 286 and guessing at it cost an afternoon and two wrong
            // fixes: the factory log said two hulls were ever ordered, so
            // the frames looked like they had to be the commander's, and
            // they were not -- a factory does not lose its queue entry when
            // the frame on the slipway dies, and 283 of them came off two
            // entries. Debug rather than info: an arena game produces a few
            // hundred of these and the runs that want them already have
            // debug on.
            if (!record.dead && unit.isDead())
            {
                record.dead = true;
                record.diedTick = now;
                LOG_DEBUG << "ARENA-DEATH tick=" << now << " player=" << record.player << " type=" << record.unitType
                          << " frame=" << (record.completed ? 0 : 1) << " born=" << record.bornTick;
            }
        }

        // A unit removed from the list entirely is gone too. Only worth
        // checking the ones we have not already buried.
        for (auto& [rawId, record] : units)
        {
            if (record.dead)
            {
                continue;
            }
            if (!sim.tryGetUnitState(UnitId(rawId)))
            {
                record.dead = true;
                record.diedTick = now;
                LOG_DEBUG << "ARENA-GONE tick=" << now << " player=" << record.player << " type=" << record.unitType
                         << " frame=" << (record.completed ? 0 : 1) << " born=" << record.bornTick;
            }
        }
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
            r.metalProduced = p.metalProduced.value;
            r.metalExcess = p.metalExcess.value;
            r.energyProduced = p.energyProduced.value;
            r.energyExcess = p.energyExcess.value;
            auto controller = sim.aiControllers.find(PlayerId(static_cast<unsigned int>(i)));
            r.phase = controller != sim.aiControllers.end() && controller->second
                ? gamePhaseName(controller->second->getBlackboard().phase)
                : "-";
            current.push_back(r);
        }

        for (const auto& [unitId, unit] : sim.units)
        {
            auto owner = static_cast<int>(unit.owner.value);
            if (owner < 0 || owner >= static_cast<int>(current.size()) || !unit.isAlive())
            {
                continue;
            }
            const auto& def = sim.unitDefinitions.at(unit.unitType);
            auto& r = current[static_cast<std::size_t>(owner)];
            ++r.units;
            const bool finished = !unit.isBeingBuilt(def);
            if (!def.isMobile)
            {
                ++r.buildings;
                if (def.builder && finished)
                {
                    ++r.factories;
                    if (unit.buildQueue.empty())
                    {
                        ++r.idleFactories;
                    }
                }
            }
            else if (def.builder)
            {
                ++r.builders;
                if (finished && unit.orders.empty())
                {
                    ++r.idleBuilders;
                }
            }
            else if (def.canAttack && (!def.weapon1.empty() || !def.weapon2.empty()))
            {
                ++r.army;
                if (finished)
                {
                    r.armyMetal += def.buildCostMetal.value;
                }
            }
        }

        for (const auto& [rawId, record] : units)
        {
            if (!record.dead || record.player < 0 || record.player >= static_cast<int>(current.size()))
            {
                continue;
            }
            auto& r = current[static_cast<std::size_t>(record.player)];
            if (record.isBuilding)
            {
                ++r.buildingsLost;
            }
            else
            {
                ++r.unitsLost;
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

        auto ticksPerSecond = static_cast<unsigned int>(SimTicksPerSecond);

        std::ofstream out(csvPath);
        if (out)
        {
            out << "tick,seconds,player,side,status,metal,energy,maxMetal,maxEnergy,"
                   "metalIncome,energyIncome,metalDemand,energyDemand,"
                   "units,buildings,army,builders,unitsLost,buildingsLost,"
                   "idleBuilders,factories,idleFactories,armyMetal,"
                   "metalProduced,metalExcess,energyProduced,energyExcess,phase\n";
            for (const auto& r : rows)
            {
                out << r.tick << ',' << (r.tick / ticksPerSecond) << ','
                    << r.player << ',' << r.side << ',' << r.status << ','
                    << r.metal << ',' << r.energy << ','
                    << r.maxMetal << ',' << r.maxEnergy << ','
                    << r.metalIncome << ',' << r.energyIncome << ','
                    << r.metalDemand << ',' << r.energyDemand << ','
                    << r.units << ',' << r.buildings << ',' << r.army << ',' << r.builders << ','
                    << r.unitsLost << ',' << r.buildingsLost << ','
                    << r.idleBuilders << ',' << r.factories << ',' << r.idleFactories << ',' << r.armyMetal << ','
                    << r.metalProduced << ',' << r.metalExcess << ',' << r.energyProduced << ',' << r.energyExcess << ','
                    << r.phase << '\n';
            }
        }
        else
        {
            LOG_ERROR << "AI arena: could not write " << csvPath.string();
        }

        auto eventsPath = csvPath;
        eventsPath.replace_filename(csvPath.stem().string() + "-events.csv");
        std::ofstream events(eventsPath);
        if (events)
        {
            events << "player,unitType,category,isBuilding,startedTick,startedSeconds,"
                      "completedTick,completedSeconds,diedTick,diedSeconds\n";
            for (const auto& [rawId, r] : units)
            {
                events << r.player << ',' << r.unitType << ',' << r.category << ',' << (r.isBuilding ? 1 : 0) << ','
                       << r.bornTick << ',' << (r.bornTick / ticksPerSecond) << ',';
                if (r.completed)
                {
                    events << r.completedTick << ',' << (r.completedTick / ticksPerSecond) << ',';
                }
                else
                {
                    events << ",,";
                }
                if (r.dead)
                {
                    events << r.diedTick << ',' << (r.diedTick / ticksPerSecond) << '\n';
                }
                else
                {
                    events << ",\n";
                }
            }
        }
        else
        {
            LOG_ERROR << "AI arena: could not write " << eventsPath.string();
        }

        // The summary line is what a batch script reads. One field per player,
        // in player order, so a run of twenty games can be reduced with grep
        // and awk and nothing else.
        std::string summary = "AI-ARENA-RESULT ticks=" + std::to_string(sim.gameTime.value)
            + " seconds=" + std::to_string(sim.gameTime.value / ticksPerSecond);

        auto playerCount = getSize(sim.players);

        // What each player actually owns, by type. The aggregate counts
        // above answer "how big", and every interesting question about an
        // AI change turned out to be "of what": whether the yards produced
        // hulls, whether an island side ever built an air plant, what the
        // land-to-naval ratio was. Answering those by grepping the log
        // afterwards is where the mistakes were -- counts that silently
        // truncated, loops that ignored which player they were counting --
        // so they are computed here, once, beside the definitions.
        //
        // Sorted, because std::map is: a diff between two runs should be a
        // diff of behaviour and not of iteration order.
        std::vector<std::map<std::string, int>> typeCounts(static_cast<std::size_t>(playerCount));
        for (const auto& [unitId, unit] : sim.units)
        {
            auto owner = static_cast<int>(unit.owner.value);
            if (owner < 0 || owner >= static_cast<int>(typeCounts.size()) || !unit.isAlive())
            {
                continue;
            }
            ++typeCounts[static_cast<std::size_t>(owner)][unit.unitType];
        }

        for (Index i = 0; i < playerCount; ++i)
        {
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

            // Appended last, so every field a reader already parses keeps
            // its position and its meaning. A dash rather than an empty
            // value when a player owns nothing, so the field is always
            // present and a parser never has to special-case its absence.
            std::string types;
            for (const auto& [unitType, count] : typeCounts[static_cast<std::size_t>(i)])
            {
                if (!types.empty())
                {
                    types += ';';
                }
                types += unitType + "x" + std::to_string(count);
            }
            summary += " types=" + (types.empty() ? std::string("-") : types);
        }
        return summary;
    }
}
