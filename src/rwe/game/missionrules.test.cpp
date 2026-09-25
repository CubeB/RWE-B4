#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/game/save_util.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MissionRules.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <string>

/**
 * A campaign mission's win and lose rules (issue #38), as the original
 * builds and checks them: TOTALA-EXE-DATA.md §113.
 */
namespace rwe
{
    namespace
    {
        using K = MissionRule::Kind;

        UnitDefinition definitionFrom(const std::string& unitName, const std::string& keys)
        {
            auto fbi = parseUnitFbi(parseTdfFromString("[UNITINFO]\n{\nUnitName=" + unitName + ";\nObjectname=model;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n"));
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(fbi, movementClasses);
        }

        /** P0 on Arm, P1 on Core, and a handful of unit types, on a flat 1024-unit map. */
        struct MissionWorld
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            std::shared_ptr<CobScript> script = makeEmptyCobScript();
            PlayerId human;
            PlayerId computer;

            MissionWorld()
            {
                auto mobile = "FootprintX=2;\nFootprintZ=2;\nBMcode=1;\nMaxDamage=200;\nBuildTime=100;\nMaxVelocity=1;";
                auto building = "FootprintX=2;\nFootprintZ=2;\nBMcode=0;\nMaxDamage=300;\nBuildTime=100;\nYardMap=oooo;";
                for (const auto& name : {"ARMCOM", "CORCOM"})
                {
                    sim.unitDefinitions[name] = definitionFrom(name, std::string(mobile) + "\nCommander=1;");
                }
                for (const auto& name : {"KBOT", "ARMZEUS"})
                {
                    sim.unitDefinitions[name] = definitionFrom(name, mobile);
                }
                for (const auto& name : {"BLDG", "ARMGATE", "ARMARAD"})
                {
                    sim.unitDefinitions[name] = definitionFrom(name, building);
                }
                sim.unitDefinitions["PAD"] = definitionFrom("PAD", std::string(building) + "\nIsAirBase=1;");
                for (const auto& [name, def] : sim.unitDefinitions)
                {
                    sim.unitScriptDefinitions[name] = *script;
                }
                human = addWellStockedPlayer(sim, "ARM");
                computer = addWellStockedPlayer(sim, "CORE");
            }

            UnitId unit(const std::string& type, PlayerId owner, float x = 0.0f, float z = 0.0f)
            {
                return addUnitOfType(sim, type, owner, SimVector(SimScalar(x), 0_ss, SimScalar(z)), script);
            }

            MissionRules& install(const OtaMissionRules& rules, bool hasUnits = true)
            {
                sim.missionRules = std::make_unique<MissionRules>(buildMissionRules(rules, sim.terrain, hasUnits, human, computer, "armcom", "CORCOM"));
                return *sim.missionRules;
            }

            /** The next once-a-second check, as the tick runs it. */
            void second()
            {
                sim.gameTime = GameTime(((sim.gameTime.value / SimTicksPerSecond) + 1) * SimTicksPerSecond);
                sim.missionRules->update(sim);
            }

            void seconds(int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    second();
                }
            }
        };

        std::vector<K> kindsOf(const std::vector<MissionRule>& rules)
        {
            std::vector<K> kinds;
            for (const auto& r : rules)
            {
                kinds.push_back(r.kind);
            }
            return kinds;
        }
    }

    TEST_CASE("mission rules are built in the original's order, with its defaults", "[mission]")
    {
        MissionWorld world;

        SECTION("no keys at all is DestroyAllUnits against AllUnitsKilled")
        {
            auto& m = world.install(OtaMissionRules{});
            REQUIRE(kindsOf(m.victory) == std::vector<K>{K::DestroyAllUnits});
            REQUIRE(kindsOf(m.defeat) == std::vector<K>{K::AllUnitsKilled});
            REQUIRE(m.enabled);
            REQUIRE(m.countdown == -1);
            REQUIRE(m.humanCommander == "ARMCOM");
        }

        SECTION("every key, each in its place")
        {
            OtaMissionRules r;
            r.victoryTimerRunsOut = 3600;
            r.unitTypePassesZ = OtaUnitTypeAndNumber{"armcom", 800};
            r.killEnemyCommander = 1;
            r.captureUnitType = "ARMARAD";
            r.moveUnitToRadius = OtaMoveUnitToRadius{"ANYTYPE", 992, 656, 64};
            r.anyUnitPassesZ = 0;
            r.deathTimerRunsOut = 5400;
            r.commanderKilled = 1;
            auto& m = world.install(r);

            REQUIRE(kindsOf(m.victory) == std::vector<K>{K::KillEnemyCommander, K::CaptureUnitType, K::MoveUnitToRadius, K::UnitTypePassesZ, K::VictoryTimerRunsOut});
            REQUIRE(kindsOf(m.defeat) == std::vector<K>{K::CommanderKilled, K::DeathTimerRunsOut, K::AnyUnitPassesZ});

            // ANYTYPE is any type, names are upper case, lines are cells and
            // timers are ticks.
            REQUIRE(m.victory[2].unitType.empty());
            REQUIRE(m.victory[2].radius == 64_ss);
            REQUIRE(m.victory[3].unitType == "ARMCOM");
            REQUIRE(m.victory[3].number == 50);
            REQUIRE(m.victory[4].number == 3600 * 30);
            REQUIRE(m.defeat[1].number == 5400 * 30);
            // AnyUnitPassesZ=0 is a line at the top of the map, not "absent".
            REQUIRE(m.defeat[2].number == 0);
        }

        SECTION("a mission with no [units] can be neither won nor lost")
        {
            auto& m = world.install(OtaMissionRules{}, false);
            REQUIRE_FALSE(m.enabled);
            world.seconds(10);
            REQUIRE(m.countdown == -1);
            REQUIRE_FALSE(m.outcome);
        }
    }

    TEST_CASE("a mission ends five checks after its result is first seen", "[mission]")
    {
        MissionWorld world;
        world.unit("KBOT", world.human);
        auto& m = world.install(OtaMissionRules{});

        // P1 has nothing, so DestroyAllUnits holds from the first check.
        world.second();
        REQUIRE(m.countdown == 4);
        REQUIRE_FALSE(m.outcome);
        REQUIRE(m.economyFrozen());
        REQUIRE(std::holds_alternative<WinStatusUndecided>(world.sim.computeWinStatus()));

        world.seconds(4);
        REQUIRE(m.countdown == 0);
        REQUIRE_FALSE(m.outcome);

        world.second();
        REQUIRE(m.outcome == MissionOutcome::Victory);
        auto status = world.sim.computeWinStatus();
        REQUIRE(std::get<WinStatusWon>(status).winner == world.human);
    }

    TEST_CASE("the countdown pauses when the result lapses and never starts over", "[mission]")
    {
        MissionWorld world;
        world.unit("KBOT", world.human);
        auto& m = world.install(OtaMissionRules{});

        world.seconds(3);
        REQUIRE(m.countdown == 2);

        // P1 gets a unit: DestroyAllUnits does not latch, so the count stops.
        auto late = world.unit("KBOT", world.computer);
        world.seconds(5);
        REQUIRE(m.countdown == 2);

        world.sim.killUnit(late);
        world.sim.units.remove(late);
        world.seconds(2);
        REQUIRE(m.countdown == 0);
        world.second();
        REQUIRE(m.outcome == MissionOutcome::Victory);
    }

    TEST_CASE("the economy stands still while a mission's countdown runs", "[mission]")
    {
        MissionWorld world;
        world.unit("KBOT", world.human);
        auto& m = world.install(OtaMissionRules{});
        auto& player = world.sim.getPlayer(world.human);

        // The settle rewrites the storage cap every second; frozen, it cannot.
        world.second();
        REQUIRE(m.economyFrozen());
        player.maxMetal = Metal(12345.0f);
        world.sim.updateResources();
        REQUIRE(player.maxMetal == Metal(12345.0f));

        m.countdown = -1;
        world.sim.updateResources();
        REQUIRE(player.maxMetal != Metal(12345.0f));
    }

    TEST_CASE("every victory rule must hold, polled in order", "[mission]")
    {
        MissionWorld world;
        auto enemy = world.unit("KBOT", world.computer);
        // P0 needs something standing, or the default AllUnitsKilled loses
        // the game under every section here.
        world.unit("KBOT", world.human, -400.0f, -400.0f);

        SECTION("a timer alone does not win, and does once the rest holds too (EXP1CC12)")
        {
            OtaMissionRules r;
            r.killEnemyCommander = 1;
            r.destroyAllUnits = 1;
            r.victoryTimerRunsOut = 60;
            auto& m = world.install(r);
            world.seconds(120);
            REQUIRE(m.countdown == -1);
            REQUIRE_FALSE(m.outcome);

            // The commander dies, then the last of the army: now the timer,
            // long run out, is what the poll reaches and it holds.
            auto commander = world.unit("CORCOM", world.computer);
            world.sim.killUnit(commander);
            world.sim.units.remove(commander);
            world.second();
            REQUIRE(m.countdown == -1);
            world.sim.killUnit(enemy);
            world.sim.units.remove(enemy);
            world.second();
            REQUIRE(m.countdown == 4);
        }

        SECTION("the timer is still ANDed: all else holding does not win before it")
        {
            OtaMissionRules r;
            r.destroyAllUnits = 1;
            r.victoryTimerRunsOut = 60;
            auto& m = world.install(r);
            world.sim.killUnit(enemy);
            world.sim.units.remove(enemy);
            world.seconds(59);
            REQUIRE(m.countdown == -1);
            REQUIRE_FALSE(m.outcome);
            world.second();
            REQUIRE(m.countdown == 4);
        }

        SECTION("a unit that passed the line before the last enemy died does not count (EXP1AC11)")
        {
            OtaMissionRules r;
            r.destroyAllUnits = 1;
            r.unitTypePassesZ = OtaUnitTypeAndNumber{"ARMCOM", 800};
            auto& m = world.install(r);

            // Z cell 50 is world z 800 - 512 = 288 at the footprint's
            // top-left, so a 2x2 unit's centre at 304.
            auto commander = world.unit("ARMCOM", world.human, 0.0f, 304.0f);
            world.second();
            REQUIRE_FALSE(m.victory[1].satisfied);

            world.sim.getUnitState(commander).position.z = -300_ss;
            world.sim.killUnit(enemy);
            world.sim.units.remove(enemy);
            world.seconds(10);
            REQUIRE(m.countdown == -1);
            REQUIRE_FALSE(m.outcome);

            // Back on the line with the enemy gone, it latches.
            world.sim.getUnitState(commander).position.z = 304_ss;
            world.second();
            REQUIRE(m.victory[1].satisfied);
            REQUIRE(m.countdown == 4);
        }

        SECTION("the band is five cells wide")
        {
            OtaMissionRules r;
            r.unitTypePassesX = OtaUnitTypeAndNumber{"ANYTYPE", 512};
            auto& m = world.install(r);
            // X cell 32 is world x 0 at the top-left corner; 34 is the edge.
            auto kbot = world.unit("KBOT", world.human, 16.0f + 16.0f * 3.0f, 0.0f);
            world.second();
            REQUIRE_FALSE(m.victory[0].satisfied);
            world.sim.getUnitState(kbot).position.x = SimScalar(16.0f + 16.0f * 2.0f);
            world.second();
            REQUIRE(m.victory[0].satisfied);
        }
    }

    TEST_CASE("the kill rules count the dying unit", "[mission]")
    {
        MissionWorld world;

        SECTION("KillAllOfType fires on the death that leaves one, this one")
        {
            OtaMissionRules r;
            r.killAllOfType = "kbot";
            auto& m = world.install(r);
            auto a = world.unit("KBOT", world.computer);
            auto b = world.unit("KBOT", world.computer);
            world.sim.killUnit(a);
            REQUIRE_FALSE(m.victory[0].satisfied);
            world.sim.killUnit(b);
            REQUIRE(m.victory[0].satisfied);
            REQUIRE(m.celebrations() == 1);
        }

        SECTION("KillAllMobileUnits counts P1's mobile units, nanoframes included")
        {
            OtaMissionRules r;
            r.killAllMobileUnits = 1;
            auto& m = world.install(r);
            auto a = world.unit("KBOT", world.computer);
            auto frame = world.unit("ARMZEUS", world.computer);
            world.sim.getUnitState(frame).buildTimeCompleted = 0;
            world.unit("BLDG", world.computer);
            world.sim.killUnit(a);
            REQUIRE_FALSE(m.victory[0].satisfied);
            world.sim.killUnit(frame);
            REQUIRE(m.victory[0].satisfied);
        }

        SECTION("KillUnitType counts P1's losses of the type down")
        {
            OtaMissionRules r;
            r.killUnitType = OtaUnitTypeAndNumber{"KBOT", 2};
            auto& m = world.install(r);
            world.sim.killUnit(world.unit("KBOT", world.human));
            REQUIRE(m.victory[0].number == 2);
            world.sim.killUnit(world.unit("KBOT", world.computer));
            REQUIRE(m.victory[0].number == 1);
            world.sim.killUnit(world.unit("KBOT", world.computer));
            REQUIRE(m.victory[0].satisfied);
        }

        SECTION("KillEnemyCommander is P1's own side's commander, not any commander")
        {
            OtaMissionRules r;
            r.killEnemyCommander = 1;
            auto& m = world.install(r);
            world.sim.killUnit(world.unit("ARMCOM", world.computer));
            REQUIRE_FALSE(m.victory[0].satisfied);
            world.sim.killUnit(world.unit("CORCOM", world.computer));
            REQUIRE(m.victory[0].satisfied);
        }
    }

    TEST_CASE("a capture is a capture and then a kill", "[mission]")
    {
        MissionWorld world;

        SECTION("capturing the objective wins, though it also counts as losing one (CC13)")
        {
            OtaMissionRules r;
            r.captureUnitType = "ARMARAD";
            r.unitTypeKilled = OtaUnitTypeAndNumber{"ARMARAD", 1};
            auto& m = world.install(r);
            world.unit("KBOT", world.human);
            world.unit("KBOT", world.computer);
            auto radar = world.unit("ARMARAD", world.computer);

            world.sim.captureUnit(radar, world.human, std::nullopt);
            REQUIRE(m.victory[0].satisfied);
            REQUIRE(m.defeat[0].satisfied);

            // Both hold every second, and victory is looked at first.
            world.seconds(6);
            REQUIRE(m.outcome == MissionOutcome::Victory);
        }

        SECTION("AllUnitsKilledOfType loses when the last one is destroyed, not captured (AC05)")
        {
            OtaMissionRules r;
            r.captureUnitType = "ARMGATE";
            r.allUnitsKilledOfType = "ARMGATE";
            auto gate = world.unit("ARMGATE", world.computer);
            {
                auto& captured = world.install(r);
                world.sim.captureUnit(gate, world.human, std::nullopt);
                REQUIRE_FALSE(captured.defeat[0].satisfied);
            }

            auto& destroyed = world.install(r);
            auto other = world.unit("ARMGATE", world.computer);
            // The captured gate is still standing, for P0 now.
            world.sim.killUnit(gate);
            REQUIRE_FALSE(destroyed.defeat[0].satisfied);
            world.sim.killUnit(other);
            REQUIRE(destroyed.defeat[0].satisfied);
        }

        SECTION("a capture by some third player still meets CaptureUnitType, and is a kill")
        {
            OtaMissionRules r;
            r.captureUnitType = "ARMARAD";
            r.killEnemyCommander = 1;
            auto& m = world.install(r);
            auto third = addWellStockedPlayer(world.sim, "ARM");
            world.sim.captureUnit(world.unit("ARMARAD", world.computer), third, std::nullopt);
            REQUIRE(m.victory[1].satisfied);
            // Taking P1's commander is killing it, as far as the rules go.
            world.sim.captureUnit(world.unit("CORCOM", world.computer), world.human, std::nullopt);
            REQUIRE(m.victory[0].satisfied);
        }
    }

    TEST_CASE("the defeat rules", "[mission]")
    {
        MissionWorld world;

        SECTION("AllUnitsKilled wants a usable unit: a nanoframe or a passenger will not do")
        {
            auto& m = world.install(OtaMissionRules{});
            world.unit("KBOT", world.computer);
            auto frame = world.unit("KBOT", world.human);
            world.sim.getUnitState(frame).buildTimeCompleted = 0;
            world.second();
            REQUIRE(m.defeat[0].satisfied);

            auto transport = world.unit("BLDG", world.computer);
            world.sim.getUnitState(frame).buildTimeCompleted = 100;
            world.sim.getUnitState(frame).carriedBy = transport;
            world.second();
            REQUIRE(m.defeat[0].satisfied);

            // On an air base it counts.
            auto pad = world.unit("PAD", world.computer);
            world.sim.getUnitState(frame).carriedBy = pad;
            world.second();
            REQUIRE_FALSE(m.defeat[0].satisfied);
        }

        SECTION("CommanderKilled, and the game goes to P1")
        {
            OtaMissionRules r;
            r.commanderKilled = 1;
            auto& m = world.install(r);
            world.unit("KBOT", world.computer);
            world.unit("KBOT", world.human);
            world.sim.killUnit(world.unit("ARMCOM", world.human));
            REQUIRE(m.defeat[0].satisfied);
            world.seconds(6);
            REQUIRE(m.outcome == MissionOutcome::Defeat);
            REQUIRE(std::get<WinStatusWon>(world.sim.computeWinStatus()).winner == world.computer);
        }

        SECTION("no commander death ends a mission by itself")
        {
            world.install(OtaMissionRules{});
            world.unit("KBOT", world.computer);
            world.unit("KBOT", world.human);
            auto commander = world.unit("CORCOM", world.computer);
            world.sim.killUnit(commander);
            world.sim.processVictoryCondition();
            REQUIRE(world.sim.getPlayer(world.computer).status == GamePlayerStatus::Alive);
        }
    }

    TEST_CASE("each victory objective sounds once, and a timer never does", "[mission]")
    {
        MissionWorld world;
        OtaMissionRules r;
        r.destroyAllUnits = 1;
        r.victoryTimerRunsOut = 1;
        auto& m = world.install(r);
        world.unit("KBOT", world.human);

        world.seconds(3);
        REQUIRE(m.celebrations() == 1);

        // DestroyAllUnits lapses and comes back: still the one sound.
        auto late = world.unit("KBOT", world.computer);
        world.second();
        world.sim.killUnit(late);
        world.sim.units.remove(late);
        world.second();
        REQUIRE(m.celebrations() == 1);
    }

    TEST_CASE("MoveUnitToRadius's point is picked on the ground from the screen plane", "[mission]")
    {
        SECTION("on flat ground at height h the ground point is h/2 south")
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(100));
            MapTerrain terrain(std::move(heights), 0_ss);
            auto p = missionPointOnGround(terrain, 500, 400);
            REQUIRE(p.x == 500_ss);
            REQUIRE(p.y == 100_ss);
            REQUIRE(p.z == 450_ss);
        }

        SECTION("water is ground at sea level")
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(10));
            MapTerrain terrain(std::move(heights), 40_ss);
            auto p = missionPointOnGround(terrain, 500, 400);
            REQUIRE(p.z == 420_ss);
        }

        SECTION("a point off the map is brought onto it")
        {
            MapTerrain terrain = makeFlatTerrain(64, 64);
            auto p = missionPointOnGround(terrain, -50, 5000);
            REQUIRE(p.x == 0_ss);
            REQUIRE(p.z == 1023_ss);
        }

        SECTION("a unit its InitialMission still holds does not count (AC01's own gate)")
        {
            MissionWorld world;
            OtaMissionRules r;
            r.moveUnitToRadius = OtaMoveUnitToRadius{"ANYTYPE", 512, 512, 64};
            auto& m = world.install(r);
            world.unit("KBOT", world.computer);
            auto gate = world.unit("ARMGATE", world.human);
            world.sim.getUnitState(gate).heldByMission = true;
            world.seconds(5);
            REQUIRE_FALSE(m.victory[0].satisfied);
            world.sim.getUnitState(gate).heldByMission = false;
            world.second();
            REQUIRE(m.victory[0].satisfied);
        }

        SECTION("the rule wants a usable P0 unit inside the circle")
        {
            MissionWorld world;
            OtaMissionRules r;
            r.moveUnitToRadius = OtaMoveUnitToRadius{"KBOT", 512, 512, 64};
            auto& m = world.install(r);
            REQUIRE(m.victory[0].x == 0_ss);
            REQUIRE(m.victory[0].z == 0_ss);

            world.unit("KBOT", world.computer);
            world.unit("KBOT", world.human, 0.0f, 65.0f);
            world.second();
            REQUIRE_FALSE(m.victory[0].satisfied);
            world.unit("KBOT", world.human, 0.0f, 64.0f);
            world.second();
            REQUIRE(m.victory[0].satisfied);
        }
    }

    TEST_CASE("mission rules are saved, hashed and dumped, and a skirmish is not changed", "[mission][save]")
    {
        MissionWorld world;
        auto skirmishHash = computeHashOf(world.sim);
        auto skirmishSave = saveSimulationToJson(world.sim);
        REQUIRE_FALSE(skirmishSave.contains("missionRules"));

        OtaMissionRules r;
        r.killUnitType = OtaUnitTypeAndNumber{"KBOT", 3};
        auto& m = world.install(r);
        REQUIRE(computeHashOf(world.sim) != skirmishHash);

        world.sim.killUnit(world.unit("KBOT", world.computer));
        auto before = computeHashOf(world.sim);
        m.countdown = 2;
        REQUIRE(computeHashOf(world.sim) != before);

        auto saved = saveSimulationToJson(world.sim);
        REQUIRE(saved.contains("missionRules"));

        // A fresh simulation, as a load builds one.
        GameSimulation loaded{makeFlatTerrain(64, 64), 0u, 0, 0};
        loaded.unitDefinitions = world.sim.unitDefinitions;
        loaded.unitScriptDefinitions = world.sim.unitScriptDefinitions;
        loadSimulationFromJson(saved, loaded);
        REQUIRE(loaded.missionRules);
        REQUIRE(*loaded.missionRules == m);
        REQUIRE(loaded.missionRules->victory[0].number == 2);
    }

    TEST_CASE("every way a unit dies reaches the rules, and only once", "[mission]")
    {
        MissionWorld world;
        OtaMissionRules r;
        r.killUnitType = OtaUnitTypeAndNumber{"KBOT", 10};
        r.unitTypeKilled = OtaUnitTypeAndNumber{"KBOT", 10};
        auto& m = world.install(r);
        auto left = [&]() { return m.victory[0].number; };

        SECTION("a weapon's kill, a scuttle, a reclaim and a frame given up")
        {
            world.sim.killUnit(world.unit("KBOT", world.computer));
            REQUIRE(left() == 9);
            world.sim.selfDestructUnit(world.unit("KBOT", world.computer));
            REQUIRE(left() == 8);
            auto eaten = world.unit("KBOT", world.computer);
            world.sim.reclaimUnitStep(eaten, world.human, 1000u);
            REQUIRE(left() == 7);
            auto frame = world.unit("KBOT", world.computer);
            world.sim.getUnitState(frame).buildTimeCompleted = 0;
            world.sim.removeUnfinishedUnit(frame);
            REQUIRE(left() == 6);
            // UnitTypeKilled counts the same deaths, with no owner test.
            REQUIRE(m.defeat[0].number == 6);
        }

        SECTION("a unit already dead is not counted again (0x486706)")
        {
            auto frame = world.unit("KBOT", world.computer);
            world.sim.getUnitState(frame).buildTimeCompleted = 0;
            world.sim.quietlyKillUnit(frame);
            REQUIRE(left() == 9);
            // A factory clearing a frame that died earlier in the same tick.
            world.sim.removeUnfinishedUnit(frame);
            REQUIRE(left() == 9);
            REQUIRE(m.defeat[0].number == 9);
        }
    }

    TEST_CASE("the rules run inside the tick, once a second, before the settle", "[mission]")
    {
        MissionWorld world;
        world.unit("KBOT", world.human);
        auto& m = world.install(OtaMissionRules{});
        tick(world.sim, 29);
        REQUIRE(m.countdown == -1);
        tick(world.sim, 1);
        REQUIRE(m.countdown == 4);
        tick(world.sim, 5 * 30 - 1);
        REQUIRE_FALSE(m.outcome);
        tick(world.sim, 1);
        REQUIRE(m.outcome == MissionOutcome::Victory);
        REQUIRE(world.sim.gameTime == GameTime(180));
    }

    TEST_CASE("the rest of the rules", "[mission]")
    {
        MissionWorld world;
        world.unit("KBOT", world.human, -400.0f, -400.0f);

        SECTION("UnitTypeKilled has no floor and no owner test")
        {
            OtaMissionRules r;
            r.unitTypeKilled = OtaUnitTypeAndNumber{"KBOT", 1};
            auto& m = world.install(r);
            world.sim.killUnit(world.unit("KBOT", world.human));
            REQUIRE(m.defeat[0].satisfied);
            world.sim.killUnit(world.unit("KBOT", world.computer));
            REQUIRE(m.defeat[0].number == -1);
        }

        SECTION("AnyUnitPassesZ: any P1 unit at all in the band, a frame included")
        {
            OtaMissionRules r;
            r.anyUnitPassesZ = 0;
            auto& m = world.install(r);
            // Z cell 0 to 2 is the top edge: a 2x2 unit's centre at world z
            // -512 + 16 + 16 * 2 = -464 has its corner in cell 2.
            auto frame = world.unit("ARMZEUS", world.computer, 0.0f, -464.0f + 16.0f);
            world.sim.getUnitState(frame).buildTimeCompleted = 0;
            world.second();
            REQUIRE_FALSE(m.defeat[0].satisfied);
            world.sim.getUnitState(frame).position.z = -464_ss;
            world.second();
            REQUIRE(m.defeat[0].satisfied);
        }

        SECTION("DeathTimerRunsOut is seconds, compared against the tick")
        {
            OtaMissionRules r;
            r.deathTimerRunsOut = 2;
            auto& m = world.install(r);
            world.unit("KBOT", world.computer);
            world.second();
            REQUIRE(m.countdown == -1);
            world.second();
            REQUIRE(m.countdown == 4);
        }

        SECTION("BuildUnitType wants a finished unit, and ANYTYPE is no type at all")
        {
            OtaMissionRules r;
            r.buildUnitType = "BLDG";
            auto& m = world.install(r);
            world.unit("KBOT", world.computer);
            auto frame = world.unit("BLDG", world.human);
            world.sim.getUnitState(frame).buildTimeCompleted = 0;
            world.second();
            REQUIRE_FALSE(m.victory[0].satisfied);
            world.sim.getUnitState(frame).buildTimeCompleted = 100;
            world.second();
            REQUIRE(m.victory[0].satisfied);

            r.buildUnitType = "ANYTYPE";
            auto& anyType = world.install(r);
            REQUIRE(anyType.victory[0].unitType == "ANYTYPE");
            world.second();
            REQUIRE_FALSE(anyType.victory[0].satisfied);
        }
    }

    TEST_CASE("MoveUnitToRadius's pick follows a slope the way 0x484B50 does", "[mission]")
    {
        // Height 2 per cell southward: a spot at z shows at z - z/16. The
        // walk stops at 416, which shows at 390, and interpolates towards
        // 432, which shows at 405: 416 + 10 * 16 / 15.
        Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
        for (int z = 0; z < 64; ++z)
        {
            for (int x = 0; x < 64; ++x)
            {
                heights.set(x, z, static_cast<unsigned char>(z * 2));
            }
        }
        MapTerrain terrain(std::move(heights), 0_ss);
        auto p = missionPointOnGround(terrain, 500, 400);
        REQUIRE(std::abs(p.z.value - (416.0f + (10.0f * 16.0f / 15.0f))) < 0.001f);
        // The height at the whole unit of that, 426: 52 + (54 - 52) * 10 / 16,
        // truncated.
        REQUIRE(p.y == 53_ss);
    }

    TEST_CASE("the rules' hash sees a count latching and an outcome", "[mission]")
    {
        MissionRules m;
        MissionRule rule;
        rule.kind = MissionRule::Kind::UnitTypeKilled;
        rule.number = 1;
        m.defeat.push_back(rule);
        auto before = computeHashOf(m);
        m.defeat[0].number = 0;
        m.defeat[0].satisfied = true;
        REQUIRE(computeHashOf(m) != before);

        auto undecided = computeHashOf(m);
        m.outcome = MissionOutcome::Victory;
        REQUIRE(computeHashOf(m) != undecided);
    }
}
