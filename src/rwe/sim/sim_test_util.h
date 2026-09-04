#pragma once

#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitMesh.h>
#include <string>
#include <vector>

/**
 * Fixtures the simulation tests share.
 *
 * Each of these was copied into a test file as it was written -- fourteen
 * copies of makeEmptyCobScript by the end -- so a change to how a unit gets
 * into the world had to be made in every one of them.
 *
 * A test that needs a unit built differently still writes its own: patrol
 * keeps its own addUnitOfType because it wants a named base piece and fire
 * orders set, and putting that here would only push the difference out of
 * sight.
 */
namespace rwe
{
    /** A script with nothing in it, for a unit whose COB is beside the point. */
    inline std::shared_ptr<CobScript> makeEmptyCobScript(const std::vector<std::string>& pieces = {})
    {
        auto script = std::make_shared<CobScript>();
        script->staticVariableCount = 0;
        for (const auto& piece : pieces)
        {
            script->pieces.push_back(piece);
        }
        return script;
    }

    /** A human player with a thousand of each resource and the room to hold it. */
    inline PlayerId addPlayer(GameSimulation& sim, const std::string& name = "player")
    {
        GamePlayerInfo p{
            std::optional<std::string>(name),
            GamePlayerType::Human,
            PlayerColorIndex(0),
            GamePlayerStatus::Alive,
            std::string("ARM"),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
        };
        return sim.addPlayer(p);
    }

    /** A finished unit of the given type, put straight into the world. */
    inline UnitId addUnitOfType(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
    {
        auto env = std::make_unique<CobEnvironment>(script.get());
        std::vector<UnitMesh> pieces;
        const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
        auto& unit = sim.getUnitState(unitId);
        unit.unitType = unitType;
        unit.owner = owner;
        unit.position = pos;
        unit.previousPosition = pos;
        unit.hitPoints = 100;
        unit.buildTimeCompleted = sim.unitDefinitions.at(unitType).buildTime;
        return unitId;
    }

    inline void tick(GameSimulation& sim, int ticks)
    {
        for (int i = 0; i < ticks; ++i)
        {
            sim.tick();
        }
    }

    inline bool anyProjectiles(const GameSimulation& sim)
    {
        for ([[maybe_unused]] const auto& p : sim.projectiles)
        {
            return true;
        }
        return false;
    }

    /** Whether the simulation puts anything in the air within the given window. */
    inline bool everFires(GameSimulation& sim, int ticks)
    {
        for (int i = 0; i < ticks; ++i)
        {
            sim.tick();
            if (anyProjectiles(sim))
            {
                return true;
            }
        }
        return false;
    }
}
