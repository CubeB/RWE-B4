#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <string>

namespace rwe
{
    namespace
    {
        MapTerrain makeAllianceTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addAlliancePlayer(GameSimulation& sim, const std::string& name, std::optional<int> team)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(100000.0f),
                Energy(100000.0f),
                Metal(100000.0f),
                Energy(100000.0f),
                Metal(100000.0f),
                Energy(100000.0f),
                team,
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeAllianceScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerAllianceModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        void defineWatcher(GameSimulation& sim, const std::string& type, unsigned int sight, unsigned int radar)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "CORE LEVEL1 NOTAIR NOTSUB";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            d.energyStorage = Energy(100000.0f);
            d.sightDistance = sight;
            d.radarDistance = radar;
            sim.unitDefinitions[type] = d;
        }

        UnitId spawn(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& position, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = position;
            unit.previousPosition = position;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            return sim.tryAddUnit(std::move(unit)).value();
        }
    }

    TEST_CASE("teams share what they can see", "[alliance]")
    {
        // An ally's scouts light your map and an ally's dishes fill your radar.
        // Without a team, the same two players see only their own ground.
        auto script = makeAllianceScript();

        SECTION("an ally's eyes and dishes are yours too")
        {
            GameSimulation sim(makeAllianceTerrain(), 0u, 0, 0);
            registerAllianceModel(sim);
            defineWatcher(sim, "watcher", 600u, 900u);

            auto us = addAlliancePlayer(sim, "us", 1);
            auto them = addAlliancePlayer(sim, "them", 1);

            // The ally's watcher stands far from anything of ours.
            auto watchPost = SimVector(200_ss, 0_ss, 200_ss);
            spawn(sim, "watcher", them, watchPost, script);
            sim.tick();

            REQUIRE(sim.arePlayersAllied(us, them));
            REQUIRE(sim.isVisibleTo(them, watchPost));
            REQUIRE(sim.isVisibleTo(us, watchPost));
            REQUIRE(sim.isOnRadarOf(us, watchPost));
        }

        SECTION("without a shared team nothing is shared")
        {
            GameSimulation sim(makeAllianceTerrain(), 0u, 0, 0);
            registerAllianceModel(sim);
            defineWatcher(sim, "watcher", 600u, 900u);

            auto us = addAlliancePlayer(sim, "us", std::nullopt);
            auto them = addAlliancePlayer(sim, "them", std::nullopt);

            auto watchPost = SimVector(200_ss, 0_ss, 200_ss);
            spawn(sim, "watcher", them, watchPost, script);
            sim.tick();

            REQUIRE(!sim.arePlayersAllied(us, them));
            REQUIRE(sim.isVisibleTo(them, watchPost));
            REQUIRE(!sim.isVisibleTo(us, watchPost));
            REQUIRE(!sim.isOnRadarOf(us, watchPost));
        }

        SECTION("different teams are not allies")
        {
            GameSimulation sim(makeAllianceTerrain(), 0u, 0, 0);
            registerAllianceModel(sim);
            defineWatcher(sim, "watcher", 600u, 900u);

            auto us = addAlliancePlayer(sim, "us", 1);
            auto them = addAlliancePlayer(sim, "them", 2);

            auto watchPost = SimVector(200_ss, 0_ss, 200_ss);
            spawn(sim, "watcher", them, watchPost, script);
            sim.tick();

            REQUIRE(!sim.arePlayersAllied(us, them));
            REQUIRE(!sim.isVisibleTo(us, watchPost));
        }

        SECTION("a player is always their own ally")
        {
            GameSimulation sim(makeAllianceTerrain(), 0u, 0, 0);
            auto us = addAlliancePlayer(sim, "us", std::nullopt);
            REQUIRE(sim.arePlayersAllied(us, us));
        }
    }
}
