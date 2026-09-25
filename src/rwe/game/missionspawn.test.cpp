#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <string>

/**
 * A mission's starting units (issue #293): spawnMissionUnits, which lays out
 * a schema's [units] the way the original does at mission start (0x488310).
 */
namespace rwe
{
    namespace
    {
        UnitDefinition definitionFrom(const std::string& unitName, const std::string& keys)
        {
            auto fbi = parseUnitFbi(parseTdfFromString("[UNITINFO]\n{\nUnitName=" + unitName + ";\nObjectname=model;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n"));
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(fbi, movementClasses);
        }

        OtaMissionUnit missionUnit(const std::string& name, int player, int x, int z)
        {
            OtaMissionUnit u;
            u.unitName = name;
            u.player = player;
            u.xPos = x;
            u.zPos = z;
            return u;
        }

        struct MissionWorld
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            std::array<std::optional<PlayerId>, 10> slots;

            MissionWorld()
            {
                std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
                sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
                auto script = makeEmptyCobScript({"base"});
                // A 2x2 building and a 2x2 kbot, as far as the rules read them.
                sim.unitDefinitions["BLDG"] = definitionFrom("BLDG", "FootprintX=2;\nFootprintZ=2;\nBMcode=0;\nMaxDamage=300;\nBuildTime=100;\nYardMap=oooo;");
                sim.unitDefinitions["KBOT"] = definitionFrom("KBOT", "FootprintX=2;\nFootprintZ=2;\nBMcode=1;\nMaxDamage=200;\nBuildTime=100;\nMaxVelocity=1;");
                sim.unitScriptDefinitions["BLDG"] = *script;
                sim.unitScriptDefinitions["KBOT"] = *script;
                // Players 1 and 3 seated, 2 empty.
                slots[0] = addPlayer(sim, "one");
                slots[2] = addPlayer(sim, "three");
            }
        };
    }

    TEST_CASE("a mission's units go to their players, where the file puts them", "[mission]")
    {
        MissionWorld world;
        auto& sim = world.sim;
        OtaSchema schema{};

        auto building = missionUnit("bldg", 1, 100, 100);
        building.angle = 90;
        auto kbot = missionUnit("KBOT", 3, 301, 205);
        kbot.healthPercentage = 50;
        schema.units = {building, kbot};

        auto result = spawnMissionUnits(sim, schema, world.slots);
        REQUIRE(result.skipped.empty());
        REQUIRE(result.spawned.size() == 2);

        const auto& b = sim.getUnitState(result.spawned[0]);
        const auto& k = sim.getUnitState(result.spawned[1]);

        SECTION("Player N is slot N-1")
        {
            REQUIRE(b.owner == *world.slots[0]);
            REQUIRE(k.owner == *world.slots[2]);
        }

        SECTION("a building is snapped to the build grid for its footprint")
        {
            // 0x47DDC0: cell = floor((100 - 2*8 + 8) / 16) = 5, centre at
            // 5*16 + 2*8 = 96, in world units from the top-left corner.
            auto expected = sim.terrain.topLeftCoordinateToWorld(SimVector(96_ss, 0_ss, 96_ss));
            REQUIRE(b.position.x == expected.x);
            REQUIRE(b.position.z == expected.z);
        }

        SECTION("a mobile unit is put exactly where the file says")
        {
            auto expected = sim.terrain.topLeftCoordinateToWorld(SimVector(301_ss, 0_ss, 205_ss));
            REQUIRE(k.position.x == expected.x);
            REQUIRE(k.position.z == expected.z);
        }

        SECTION("both are finished, stand on the ground, and take their health and heading")
        {
            REQUIRE_FALSE(b.isBeingBuilt(sim.unitDefinitions.at("BLDG")));
            REQUIRE_FALSE(k.isBeingBuilt(sim.unitDefinitions.at("KBOT")));
            REQUIRE(b.position.y == sim.terrain.getHeightAt(b.position.x, b.position.z));
            REQUIRE(b.hitPoints == 300u);
            REQUIRE(k.hitPoints == 100u);
            // 0x436EF9: 90 degrees is a quarter turn, 16384.
            REQUIRE(b.rotation == SimAngle(16384));
            REQUIRE(k.rotation == SimAngle(0));
        }
    }

    TEST_CASE("a mission unit with no such type or no player to own it is not made", "[mission]")
    {
        MissionWorld world;
        OtaSchema schema{};
        schema.units = {
            missionUnit("NOSUCH", 1, 100, 100),
            missionUnit("KBOT", 2, 200, 200),
            missionUnit("KBOT", 0, 200, 300),
            missionUnit("KBOT", 11, 200, 400),
            missionUnit("KBOT", 1, 300, 300),
        };

        auto result = spawnMissionUnits(world.sim, schema, world.slots);
        REQUIRE(result.spawned.size() == 1);
        REQUIRE(result.skipped.size() == 4);
        REQUIRE(result.skipped[0].find("no such unit") != std::string::npos);
        REQUIRE(result.skipped[1].find("no player in that slot") != std::string::npos);
        REQUIRE(world.sim.getUnitState(result.spawned[0]).owner == *world.slots[0]);
    }

    TEST_CASE("a mission unit whose spot is taken goes to the nearest free one", "[mission]")
    {
        // The original lets mission units overlap; RWE keeps one to a cell,
        // and CC03 alone parks eleven units on top of others or on trees.
        MissionWorld world;
        OtaSchema schema{};
        schema.units = {missionUnit("KBOT", 1, 300, 300), missionUnit("KBOT", 1, 300, 300), missionUnit("BLDG", 1, 300, 300)};

        auto result = spawnMissionUnits(world.sim, schema, world.slots);
        REQUIRE(result.spawned.size() == 2);
        REQUIRE(result.skipped.size() == 1);
        const auto& first = world.sim.getUnitState(result.spawned[0]);
        const auto& second = world.sim.getUnitState(result.spawned[1]);
        auto dx = second.position.x - first.position.x;
        auto dz = second.position.z - first.position.z;
        // Beside it, not on it, and no further than the first ring that fits
        // a 2x2 footprint.
        REQUIRE((dx * dx) + (dz * dz) > 0_ss);
        REQUIRE((dx * dx) + (dz * dz) <= 48_ss * 48_ss * 2_ss);
    }

    TEST_CASE("a mission unit with orders to run is not the player's yet", "[mission]")
    {
        // 0x487E69 clears the selectable bit once the interpreter has queued
        // an order; the standing-orders line queues none.
        MissionWorld world;
        OtaSchema schema{};
        auto scripted = missionUnit("KBOT", 1, 100, 100);
        scripted.orders = parseInitialMission("w 30,p 1500 900,");
        auto standing = missionUnit("KBOT", 1, 200, 100);
        standing.orders = parseInitialMission("o 0 1,");
        auto plain = missionUnit("KBOT", 1, 300, 100);
        schema.units = {scripted, standing, plain};

        auto result = spawnMissionUnits(world.sim, schema, world.slots);
        REQUIRE(result.spawned.size() == 3);
        REQUIRE(world.sim.getUnitState(result.spawned[0]).heldByMission);
        REQUIRE_FALSE(world.sim.getUnitState(result.spawned[1]).heldByMission);
        REQUIRE_FALSE(world.sim.getUnitState(result.spawned[2]).heldByMission);
    }
}
