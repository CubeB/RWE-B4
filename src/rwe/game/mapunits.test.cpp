#include <catch2/catch_test_macros.hpp>

#include <rwe/ColorPalette.h>
#include <rwe/LoadingScene_util.h>
#include <rwe/MeshService.h>
#include <rwe/PathMapping.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MissionScripts.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/util/SpanStream.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>
#include <cstdlib>
#include <string>

/**
 * A skirmish map's own [units] under the Community Patch (issue #934, TADR
 * 2024.10.27): whose units they become, and that they spawn as finished units
 * with their InitialMission, without the ordinary commander.
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

        OtaMissionUnit mapUnit(const std::string& name, int player, int x, int z)
        {
            OtaMissionUnit u;
            u.unitName = name;
            u.player = player;
            u.xPos = x;
            u.zPos = z;
            return u;
        }

        PlayerId addComputerPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Computer,
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

        struct MapWorld
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            std::array<std::optional<PlayerId>, 10> slots;
            /** The map position the deal gave each seat. */
            std::array<std::optional<int>, 10> startPositions;

            MapWorld()
            {
                std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
                sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
                auto script = makeEmptyCobScript({"base"});
                sim.unitDefinitions["BLDG"] = definitionFrom("BLDG", "FootprintX=2;\nFootprintZ=2;\nBMcode=0;\nMaxDamage=300;\nBuildTime=100;\nYardMap=oooo;");
                sim.unitDefinitions["KBOT"] = definitionFrom("KBOT", "FootprintX=2;\nFootprintZ=2;\nBMcode=1;\nMaxDamage=200;\nBuildTime=100;\nMaxVelocity=1;");
                sim.unitScriptDefinitions["BLDG"] = *script;
                sim.unitScriptDefinitions["KBOT"] = *script;

                // Two seats: slot 0 is a human, slot 2 a computer. The deal
                // gave slot 0 map position 3 and slot 2 map position 1, so a
                // map's Player number is not the seat index.
                slots[0] = addPlayer(sim, "human");
                slots[2] = addComputerPlayer(sim, "computer");
                startPositions[0] = 3;
                startPositions[2] = 1;
            }
        };
    }

    TEST_CASE("a skirmish schema's [units] are read into the mission-unit records", "[ota][mapunits]")
    {
        auto ota = parseOta(parseTdfFromString(R"TDF(
[GlobalHeader]
    {
    missionname=Unit Map;
    SCHEMACOUNT=1;
    [Schema 0]
        {
        Type=Network 2;
        [units]
            {
            [unit0]
                {
                UnitName=ARMHLT;
                Ident=;
                XPos=3164;
                YPos=0;
                ZPos=2250;
                Player=11;
                HealthPercentage=100;
                }
            [unit1]
                {
                UnitName=ARMPEEP;
                Ident=SCOUT;
                XPos=268;
                YPos=17;
                ZPos=384;
                Player=1;
                HealthPercentage=50;
                CreationCountdown=30;
                InitialMission=p 1750 1828;
                }
            }
        }
    }
)TDF"));

        REQUIRE(ota.schemas.size() == 1);
        const auto& units = ota.schemas.at(0).units;
        REQUIRE(units.size() == 2);

        REQUIRE(units[0].unitName == "ARMHLT");
        REQUIRE(units[0].player == 11);
        REQUIRE(units[0].xPos == 3164);
        REQUIRE(units[0].zPos == 2250);
        REQUIRE(units[0].orders.empty());

        REQUIRE(units[1].unitName == "ARMPEEP");
        REQUIRE(units[1].ident == "SCOUT");
        REQUIRE(units[1].player == 1);
        REQUIRE(units[1].yPos == 17);
        REQUIRE(units[1].healthPercentage == 50);
        REQUIRE(units[1].creationCountdown == 30);
        REQUIRE(units[1].orders.size() == 1);
        REQUIRE(units[1].orders[0].kind == MissionOrder::Kind::Patrol);
        REQUIRE(units[1].orders[0].numbers == std::vector<float>{1750.0f, 1828.0f});
    }

    TEST_CASE("a map unit belongs to the seat dealt its start position, not the seat index", "[mapunits]")
    {
        MapWorld world;
        OtaSchema schema;
        schema.units = {
            mapUnit("KBOT", 3, 400, 400),
            mapUnit("KBOT", 1, 500, 400),
            mapUnit("KBOT", 2, 600, 400), // nobody was dealt position 2
        };

        std::array<bool, 10> received{};
        auto result = spawnMapUnits(world.sim, schema, world.slots, world.startPositions, &received);

        REQUIRE(result.spawned.size() == 2);
        REQUIRE(result.skipped.size() == 1);
        REQUIRE(result.skipped[0].find("map position 2") != std::string::npos);

        REQUIRE(world.sim.getUnitState(result.spawned[0]).owner == *world.slots[0]);
        REQUIRE(world.sim.getUnitState(result.spawned[1]).owner == *world.slots[2]);

        REQUIRE(received[0]);
        REQUIRE_FALSE(received[1]);
        REQUIRE(received[2]);
    }

    TEST_CASE("Player 11 is the last seat only when that seat is a computer", "[mapunits]")
    {
        SECTION("the last seat is a computer")
        {
            MapWorld world;
            OtaSchema schema;
            schema.units = {mapUnit("KBOT", 11, 400, 400)};

            std::array<bool, 10> received{};
            auto result = spawnMapUnits(world.sim, schema, world.slots, world.startPositions, &received);

            REQUIRE(result.spawned.size() == 1);
            REQUIRE(world.sim.getUnitState(result.spawned[0]).owner == *world.slots[2]);
            REQUIRE(received[2]);
            REQUIRE_FALSE(received[0]);
        }

        SECTION("the last seat is a human")
        {
            MapWorld world;
            world.sim.getPlayer(*world.slots[2]).type = GamePlayerType::Human;
            OtaSchema schema;
            schema.units = {mapUnit("KBOT", 11, 400, 400)};

            std::array<bool, 10> received{};
            auto result = spawnMapUnits(world.sim, schema, world.slots, world.startPositions, &received);

            REQUIRE(result.spawned.empty());
            REQUIRE(result.skipped.size() == 1);
            REQUIRE(result.skipped[0].find("not a computer") != std::string::npos);
            REQUIRE_FALSE(received[2]);
        }

        SECTION("a Player outside 1-11 owns nothing")
        {
            MapWorld world;
            OtaSchema schema;
            schema.units = {mapUnit("KBOT", 0, 400, 400), mapUnit("KBOT", 12, 500, 400)};

            auto result = spawnMapUnits(world.sim, schema, world.slots, world.startPositions, nullptr);

            REQUIRE(result.spawned.empty());
            REQUIRE(result.skipped.size() == 2);
        }
    }

    TEST_CASE("a map unit is made at full health and ignores YPos and CreationCountdown", "[mapunits]")
    {
        MapWorld world;
        OtaSchema schema;
        auto kbot = mapUnit("KBOT", 1, 500, 500);
        kbot.yPos = 999;
        kbot.healthPercentage = 50;
        kbot.creationCountdown = 30;
        schema.units = {kbot};

        auto result = spawnMapUnits(world.sim, schema, world.slots, world.startPositions, nullptr);

        REQUIRE(result.spawned.size() == 1);
        const auto& unit = world.sim.getUnitState(result.spawned[0]);
        // HealthPercentage is ignored under TADR's map-unit rule.
        REQUIRE(unit.hitPoints == 200u);
        // YPos is ignored: the unit stands on the terrain.
        REQUIRE(unit.position.y == world.sim.terrain.getHeightAt(unit.position.x, unit.position.z));
        auto expected = world.sim.terrain.topLeftCoordinateToWorld(SimVector(500_ss, 0_ss, 500_ss));
        REQUIRE(unit.position.x == expected.x);
        REQUIRE(unit.position.z == expected.z);
    }

    TEST_CASE("a map unit's InitialMission runs and can name another by Ident", "[mapunits]")
    {
        MapWorld world;
        OtaSchema schema;

        auto leader = mapUnit("KBOT", 1, 400, 400);
        leader.ident = "LEADER";
        auto guard = mapUnit("KBOT", 1, 500, 400);
        guard.ident = "GUARD";
        guard.initialMission = "g LEADER,";
        guard.orders = parseInitialMission(guard.initialMission);
        auto unknown = mapUnit("KBOT", 1, 600, 400);
        unknown.initialMission = "g NOSUCH,";
        unknown.orders = parseInitialMission(unknown.initialMission);
        schema.units = {leader, guard, unknown};

        auto result = spawnMapUnits(world.sim, schema, world.slots, world.startPositions, nullptr);
        REQUIRE(result.spawned.size() == 3);
        REQUIRE(world.sim.missionScripts);

        const auto& guardScript = world.sim.missionScripts->scripts.at(result.spawned[1].value);
        // The guard, then the MAKESELECTABLE the interpreter appends to a list
        // that queued something and had no `s`, `p`, point `a` or `d`.
        REQUIRE(guardScript.steps.size() == 2);
        REQUIRE(guardScript.steps.front().kind == MissionStep::Kind::Guard);
        REQUIRE(guardScript.steps.front().target == result.spawned[0]);
        REQUIRE(guardScript.steps.back().kind == MissionStep::Kind::MakeSelectable);
        REQUIRE(world.sim.getUnitState(result.spawned[1]).heldByMission);

        // A name that is not a spawned unit's Ident or type queues nothing, and
        // the unit is not held for a mission it did not get.
        REQUIRE_FALSE(world.sim.missionScripts->scripts.contains(result.spawned[2].value));
        REQUIRE_FALSE(world.sim.getUnitState(result.spawned[2]).heldByMission);
    }

    TEST_CASE("the real skirmish loader spawns a map's units and marks the commanderless seats", "[mapunits][retail]")
    {
        const auto* dataPath = std::getenv("RWE_SCENARIO_DATA");
        if (!dataPath)
        {
            SKIP("set RWE_SCENARIO_DATA to the retail archive directory");
        }

        auto vfs = constructVfs(dataPath);

        auto otaBytes = vfs.readFile("maps/Coast To Coast.ota");
        auto tntBytes = vfs.readFile("maps/Coast To Coast.tnt");
        if (!otaBytes || !tntBytes)
        {
            SKIP("the test map is not installed");
        }
        auto ota = parseOta(parseTdfFromString(std::string(otaBytes->begin(), otaBytes->end())));
        if (ota.schemas.empty() || countStartPositions(ota.schemas.at(0)) < 2)
        {
            SKIP("the test map has no two-seat schema");
        }

        // Keep the map's own schema (so its start positions are the real ones)
        // and add the synthetic [units] the test is about.
        auto unitsTdf = parseTdfFromString(R"TDF(
[units]
    {
    [unit0]
        {
        UnitName=ARMPW;
        XPos=300;
        YPos=0;
        ZPos=300;
        Player=1;
        HealthPercentage=50;
        }
    [unit1]
        {
        UnitName=ARMCOM;
        XPos=400;
        YPos=0;
        ZPos=400;
        Player=11;
        HealthPercentage=100;
        }
    }
)TDF");
        auto unitsBlock = unitsTdf.findBlock("units");
        REQUIRE(unitsBlock);
        for (int i = 0;; ++i)
        {
            auto block = unitsBlock->get().findBlock("unit" + std::to_string(i));
            if (!block)
            {
                break;
            }
            ota.schemas.at(0).units.push_back(parseOtaMissionUnit(*block));
        }

        SpanStream tntStream(tntBytes->data(), tntBytes->size());
        TntArchive tnt(&tntStream);
        auto mapData = readMapData(tnt, ota, 0);

        PathMapping paths;
        paths.ai = "ai";
        paths.anims = "anims";
        paths.bitmaps = "bitmaps";
        paths.camps = "camps";
        paths.downloads = "download";
        paths.features = "features";
        paths.fonts = "fonts";
        paths.gamedata = "gamedata";
        paths.guis = "guis";
        paths.maps = "maps";
        paths.objects3d = "objects3d";
        paths.palettes = "palettes";
        paths.scripts = "scripts";
        paths.sounds = "sounds";
        paths.textures = "textures";
        paths.unitpics = "unitpics";
        paths.units = "units";
        paths.weapons = "weapons";

        ColorPalette palette;
        for (int i = 0; i < 256; ++i)
        {
            palette.emplace_back(static_cast<unsigned char>(i), static_cast<unsigned char>(i), static_cast<unsigned char>(i));
        }

        GameParameters params{"Coast To Coast", 0};
        params.communityPatch = true;
        params.players[0] = PlayerInfo{std::string("human"), PlayerControllerTypeHuman{}, "ARM", PlayerColorIndex(0), Metal(1000.0f), Energy(1000.0f)};
        params.players[1] = PlayerInfo{std::string("ai"), PlayerControllerTypeComputer{}, "CORE", PlayerColorIndex(1), Metal(1000.0f), Energy(1000.0f)};

        MeshService meshes(&vfs, nullptr, {}, {}, {});
        GameLoadServices services{&vfs, &paths, &palette, &palette, nullptr, &meshes, nullptr, nullptr};

        auto loaded = loadGameSimulation(services, params, std::move(mapData), ota);

        REQUIRE(loaded.mapUnitSpawn.spawned.size() == 2);
        REQUIRE(loaded.mapUnitSlots[0]);
        REQUIRE(loaded.mapUnitSlots[1]);

        // Player 1 is the human's dealt start position; Player 11 is the last
        // seat, the computer.
        const auto& first = loaded.simulation.getUnitState(loaded.mapUnitSpawn.spawned[0]);
        const auto& second = loaded.simulation.getUnitState(loaded.mapUnitSpawn.spawned[1]);
        REQUIRE(first.owner == *loaded.gamePlayers[0]);
        REQUIRE(second.owner == *loaded.gamePlayers[1]);
        REQUIRE(first.unitType == "ARMPW");
        REQUIRE(second.unitType == "ARMCOM");
        REQUIRE(first.hitPoints == loaded.simulation.unitDefinitions.at("ARMPW").maxHitPoints);
        REQUIRE(second.hitPoints == loaded.simulation.unitDefinitions.at("ARMCOM").maxHitPoints);
    }
}
