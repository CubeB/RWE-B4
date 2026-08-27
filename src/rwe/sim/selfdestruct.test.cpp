#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("player"),
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

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
        }

        UnitId addSolar(GameSimulation& sim, PlayerId owner, const std::shared_ptr<CobScript>& script)
        {
            UnitDefinition d{};
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.corpse = "solarwreck";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            sim.unitDefinitions["solar"] = d;

            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = "solar";
            unit.owner = owner;
            unit.position = SimVector(100_ss, 0_ss, 100_ss);
            unit.previousPosition = unit.position;
            unit.hitPoints = 100;
            return unitId;
        }

        std::optional<UnitDiedEvent::DeathType> lastDeathType(const GameSimulation& sim)
        {
            std::optional<UnitDiedEvent::DeathType> result;
            for (const auto& e : sim.events)
            {
                if (auto died = std::get_if<UnitDiedEvent>(&e))
                {
                    result = died->deathType;
                }
            }
            return result;
        }
    }

    TEST_CASE("self-destruct countdown", "[selfdestruct]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto solarId = addSolar(sim, player, script);

        SECTION("toggling starts a five second countdown and toggling again cancels it")
        {
            sim.toggleSelfDestruct(solarId);
            REQUIRE(sim.getUnitState(solarId).selfDestructTime.has_value());
            REQUIRE(*sim.getUnitState(solarId).selfDestructTime == sim.gameTime + GameTime(5 * SimTicksPerSecond));

            sim.toggleSelfDestruct(solarId);
            REQUIRE_FALSE(sim.getUnitState(solarId).selfDestructTime.has_value());

            for (int i = 0; i < 200; ++i)
            {
                sim.tick();
            }
            REQUIRE(sim.getUnitState(solarId).isAlive());
        }

        SECTION("the unit dies when the countdown expires, leaving no wreck")
        {
            sim.toggleSelfDestruct(solarId);

            for (int i = 0; i < GameSimulation::SelfDestructCountdownTicks - 1; ++i)
            {
                sim.tick();
            }
            REQUIRE(sim.getUnitState(solarId).isAlive());

            sim.tick();
            REQUIRE(lastDeathType(sim) == UnitDiedEvent::DeathType::SelfDestructed);
            // deleteDeadUnits ran in the same tick; no corpse feature was spawned.
            REQUIRE_FALSE(sim.tryGetUnitState(solarId).has_value());
            REQUIRE(sim.features.begin() == sim.features.end());
        }

        SECTION("toggling a dead unit does nothing")
        {
            sim.getUnitState(solarId).markAsDead();
            sim.toggleSelfDestruct(solarId);
            REQUIRE_FALSE(sim.getUnitState(solarId).selfDestructTime.has_value());
        }
    }

    TEST_CASE("kills and losses are tallied per player", "[selfdestruct]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto enemy = addPlayer(sim);
        auto attackerId = addSolar(sim, player, script);
        auto victimId = addSolar(sim, enemy, script);
        auto frameId = addSolar(sim, enemy, script);

        sim.killUnit(victimId, attackerId);
        // The corpse feature is not defined in this test; the sim must cope.
        sim.tick();
        REQUIRE_FALSE(sim.tryGetUnitState(victimId).has_value());
        REQUIRE(sim.getPlayer(player).unitsKilled == 1u);
        REQUIRE(sim.getPlayer(enemy).unitsLost == 1u);

        // A nanoframe dying quietly is still a loss, but nobody's kill.
        sim.getUnitState(frameId).buildTimeCompleted = 0u;
        sim.unitDefinitions["solar"].buildTime = 100u;
        sim.applyDamage(frameId, 1000u, attackerId);
        REQUIRE(sim.getPlayer(enemy).unitsLost == 2u);
        REQUIRE(sim.getPlayer(player).unitsKilled == 1u);

        // Self-destruction is a loss too.
        sim.toggleSelfDestruct(attackerId);
        for (unsigned int i = 0; i < GameSimulation::SelfDestructCountdownTicks; ++i)
        {
            sim.tick();
        }
        REQUIRE(sim.getPlayer(player).unitsLost == 1u);
    }

    TEST_CASE("a unit under construction that is destroyed still announces its death", "[selfdestruct]")
    {
        // The scene relies on UnitDiedEvent to drop dead units from its
        // selection and hover state; a quiet death must not skip it.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto solarId = addSolar(sim, player, script);
        sim.unitDefinitions["solar"].buildTime = 100u;
        sim.getUnitState(solarId).buildTimeCompleted = 10u;
        sim.getUnitState(solarId).hitPoints = 5;

        sim.applyDamage(solarId, 5u);

        REQUIRE(sim.getUnitState(solarId).isDead());
        REQUIRE(lastDeathType(sim) == UnitDiedEvent::DeathType::Deleted);
        sim.tick();
        REQUIRE_FALSE(sim.tryGetUnitState(solarId).has_value());
        REQUIRE(sim.features.begin() == sim.features.end());
    }
}
