#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/GameSimulationLoader.h>
#include <set>
#include <rwe/game/BuilderGuisDatabase.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MissionScripts.h>
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

    TEST_CASE("a mission building stands where the file puts it, whatever scenery was there", "[mission]")
    {
        // Issue #377. The original's creator never asks what is under a
        // unit (0x485F50), so a mission's towers stand among its trees and
        // rocks; RWE clears the scenery from under the building instead of
        // leaving the building out.
        MissionWorld world;
        FeatureDefinition rock{};
        rock.name = "ROCK";
        rock.footprintX = 2;
        rock.footprintZ = 2;
        rock.height = 20_ss;
        rock.blocking = true;
        auto rockDef = world.sim.featureDefinitions.insert(rock);
        // Cells 5,5 to 6,6: exactly where a 2x2 building at (100, 100) goes.
        auto rockId = world.sim.addFeature(rockDef, 5, 5).value();

        OtaSchema schema{};
        schema.units = {missionUnit("BLDG", 1, 100, 100)};
        auto result = spawnMissionUnits(world.sim, schema, world.slots);

        REQUIRE(result.skipped.empty());
        REQUIRE(result.spawned.size() == 1);
        CHECK_FALSE(world.sim.tryGetFeature(rockId).has_value());
        REQUIRE(result.adjusted.size() == 1);
        CHECK(result.adjusted.front().find("cleared ROCK") != std::string::npos);

        // Where the file put it, not beside the rock.
        auto expected = world.sim.terrain.topLeftCoordinateToWorld(SimVector(96_ss, 0_ss, 96_ss));
        const auto& building = world.sim.getUnitState(result.spawned.front());
        CHECK(building.position.x == expected.x);
        CHECK(building.position.z == expected.z);
    }

    TEST_CASE("a mission building over the edge of the map comes in just far enough to fit", "[mission]")
    {
        MissionWorld world;
        auto gridWidth = world.sim.occupiedGrid.getWidth();
        // Its footprint's first cell is the grid's last.
        auto xPos = (gridWidth * 16) + 8;

        OtaSchema schema{};
        schema.units = {missionUnit("BLDG", 1, xPos, 100)};
        auto result = spawnMissionUnits(world.sim, schema, world.slots);

        REQUIRE(result.skipped.empty());
        REQUIRE(result.spawned.size() == 1);
        REQUIRE(result.adjusted.size() == 1);
        CHECK(result.adjusted.front().find("moved onto the map") != std::string::npos);
        const auto& building = world.sim.getUnitState(result.spawned.front());
        auto footprint = world.sim.computeFootprintRegion(building.position, world.sim.unitDefinitions.at("BLDG").movementCollisionInfo);
        CHECK(footprint.x + static_cast<int>(footprint.width) == gridWidth);
    }

    TEST_CASE("a building brought in from the edge is placed or refused as anywhere else, with one note", "[mission]")
    {
        MissionWorld world;
        auto gridWidth = world.sim.occupiedGrid.getWidth();
        auto xPos = (gridWidth * 16) + 8;
        // Where it lands once brought in: its last two columns, rows 5 and 6.
        auto landing = gridWidth - 2;

        SECTION("a unit where it lands still keeps it out, and nothing claims it was moved")
        {
            // A 2x2 kbot centred on the landing cells, earlier in the file.
            OtaSchema schema{};
            schema.units = {missionUnit("KBOT", 1, (landing * 16) + 16, 96), missionUnit("BLDG", 1, xPos, 100)};
            auto result = spawnMissionUnits(world.sim, schema, world.slots);
            CHECK(result.spawned.size() == 1u);
            CHECK(result.skipped.size() == 1u);
            CHECK(result.adjusted.empty());
        }

        SECTION("scenery where it lands is cleared, and the one note says both")
        {
            FeatureDefinition rock{};
            rock.name = "ROCK";
            rock.footprintX = 2;
            rock.footprintZ = 2;
            rock.height = 20_ss;
            rock.blocking = true;
            auto rockDef = world.sim.featureDefinitions.insert(rock);
            auto rockId = world.sim.addFeature(rockDef, landing, 5).value();

            OtaSchema schema{};
            schema.units = {missionUnit("BLDG", 1, xPos, 100)};
            auto result = spawnMissionUnits(world.sim, schema, world.slots);
            REQUIRE(result.spawned.size() == 1u);
            CHECK_FALSE(world.sim.tryGetFeature(rockId).has_value());
            REQUIRE(result.adjusted.size() == 1u);
            CHECK(result.adjusted.front().find("moved onto the map") != std::string::npos);
            CHECK(result.adjusted.front().find("cleared ROCK") != std::string::npos);
        }
    }

    TEST_CASE("a mission's unit list is the block names of its useonly file", "[mission]")
    {
        auto tdf = parseTdfFromString("[ARMCOM]\n{\n}\n[armpw]\n{\n}\n");
        CHECK(missionUnitListFromTdf(tdf) == std::set<std::string>{"ARMCOM", "ARMPW"});
    }

    TEST_CASE("a unit off the mission's list cannot be built or placed", "[mission]")
    {
        // Issue #381. The original removes it from the game (0x431740,
        // 0x42D2E0); RWE keeps the definition and refuses it wherever a unit
        // is made.
        MissionWorld world;
        BuilderGuisDatabase guis;
        std::vector<GuiEntry> page(3);
        page[0].common.name = "BLDG";
        page[1].common.name = "kbot";
        page[2].common.name = "NEXT";
        guis.addBuilderGui("BLDG", {page});

        auto kept = applyMissionUnitList(world.sim.unitDefinitions, guis, {"BLDG"});
        CHECK(kept == 1u);
        CHECK(world.sim.unitDefinitions.at("KBOT").excludedByMission);
        CHECK_FALSE(world.sim.unitDefinitions.at("BLDG").excludedByMission);

        SECTION("its button leaves the build menus, and nothing else does")
        {
            const auto& menu = guis.tryGetBuilderGui("BLDG")->get().front();
            REQUIRE(menu.size() == 2u);
            CHECK(menu[0].common.name == "BLDG");
            CHECK(menu[1].common.name == "NEXT");
        }

        SECTION("the mission does not place one")
        {
            OtaSchema schema{};
            schema.units = {missionUnit("KBOT", 1, 300, 300), missionUnit("BLDG", 1, 100, 100)};
            auto result = spawnMissionUnits(world.sim, schema, world.slots);
            REQUIRE(result.spawned.size() == 1u);
            REQUIRE(result.skipped.size() == 1u);
            CHECK(result.skipped.front().find("not on the mission's unit list") != std::string::npos);
        }

        SECTION("nobody can order one built")
        {
            OtaSchema schema{};
            schema.units = {missionUnit("BLDG", 1, 100, 100)};
            auto factory = spawnMissionUnits(world.sim, schema, world.slots).spawned.front();
            auto owner = *world.slots[0];
            auto site = SimVector(200_ss, 0_ss, 200_ss);
            using Issue = PlayerUnitCommand::IssueOrder;

            CHECK_FALSE(applyUnitCommandToSimulation(world.sim, owner, PlayerUnitCommand(factory, Issue(BuildOrder("KBOT", site), Issue::Immediate))));
            CHECK_FALSE(applyUnitCommandToSimulation(world.sim, owner, PlayerUnitCommand(factory, PlayerUnitCommand::ModifyBuildQueue{1, "KBOT"})));
            // What the list does offer is still built.
            CHECK(applyUnitCommandToSimulation(world.sim, owner, PlayerUnitCommand(factory, Issue(BuildOrder("BLDG", site), Issue::Immediate))));
        }
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
        // An `s` first hands the unit back before it does anything else.
        auto selectableFirst = missionUnit("KBOT", 1, 400, 100);
        selectableFirst.orders = parseInitialMission("s,m 1500 900,");
        auto selectableLast = missionUnit("KBOT", 1, 500, 100);
        selectableLast.orders = parseInitialMission("m 1500 900,s,");
        // `i` puts the unit aboard a transport at once and queues nothing, and
        // neither do a letter the table does not know or a type nobody has.
        auto linkOnly = missionUnit("KBOT", 1, 600, 100);
        linkOnly.orders = parseInitialMission("i CHRIS,");
        auto unknownThings = missionUnit("KBOT", 1, 700, 100);
        unknownThings.orders = parseInitialMission("x,a NOSUCHTYPE,");
        // A guard resolves against every unit the mission made, including
        // one further down the list.
        auto guard = missionUnit("KBOT", 1, 800, 100);
        guard.orders = parseInitialMission("g BOSS,");
        auto boss = missionUnit("BLDG", 1, 900, 100);
        boss.ident = "boss";
        // Or by a unit type, the first of it in the file.
        auto guardByType = missionUnit("KBOT", 1, 150, 300);
        guardByType.orders = parseInitialMission("g bldg,");
        schema.units = {scripted, standing, plain, selectableFirst, selectableLast, linkOnly, unknownThings, guard, boss, guardByType};

        auto result = spawnMissionUnits(world.sim, schema, world.slots);
        REQUIRE(result.spawned.size() == 10);
        auto held = [&](std::size_t i) { return world.sim.getUnitState(result.spawned[i]).heldByMission; };
        REQUIRE(held(0));
        REQUIRE_FALSE(held(1));
        REQUIRE_FALSE(held(2));
        // Held once its list is read, like any other that queued something,
        // and handed back by the `s` as soon as the list starts to run.
        REQUIRE(held(3));
        REQUIRE(held(4));
        REQUIRE_FALSE(held(5));
        REQUIRE_FALSE(held(6));
        REQUIRE(held(7));
        REQUIRE_FALSE(held(8));
        REQUIRE(held(9));

        REQUIRE(world.sim.missionScripts);
        world.sim.missionScripts->update(world.sim);
        REQUIRE_FALSE(held(3));
        REQUIRE(held(4));
    }
}
