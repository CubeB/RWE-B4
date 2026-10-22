#include <catch2/catch_test_macros.hpp>
#include <rwe/game/save_util.h>
#include <rwe/game/EliminationRules.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    TEST_CASE("elimination watching uses host permission and the corrected hosted-AI exception", "[spectators]")
    {
        CHECK(eliminationFlow(false, true, false, false) == EliminationFlow::End);
        CHECK(eliminationFlow(true, false, false, false) == EliminationFlow::End);
        CHECK(eliminationFlow(true, true, false, true) == EliminationFlow::End);
        CHECK(eliminationFlow(true, true, false, false) == EliminationFlow::AskWatch);
        CHECK(eliminationFlow(true, false, true, false) == EliminationFlow::ForcedWatch);
        CHECK(eliminationFlow(true, true, true, false) == EliminationFlow::ForcedWatch);
    }
    namespace
    {
        void defineRespawnCommander(GameSimulation& sim)
        {
            // Stock ARM commander: 3000 HP, 2x2 footprint. Rendering and
            // weapons are immaterial to the placement/countdown fixture.
            UnitDefinition def{};
            def.objectName = "ARMCOM";
            def.commander = true;
            def.isMobile = true;
            def.maxHitPoints = 3000;
            def.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2, 2, 36, 36, 0, 255};
            def.energyStorage = Energy(1000);
            def.metalStorage = Metal(1000);
            sim.unitDefinitions["ARMCOM"] = def;
            sim.unitModelDefinitions["ARMCOM"] = UnitModelDefinition{};
            sim.unitScriptDefinitions["ARMCOM"] = *makeEmptyCobScript();
        }
    }

    TEST_CASE("commander-death modes preserve, wipe or replace the army", "[deathmatch][endgame]")
    {
        for (const auto mode : {CommanderDeathMode::GameContinues, CommanderDeathMode::GameEnds, CommanderDeathMode::Deathmatch})
        {
            GameSimulation sim(makeFlatTerrain(128, 128), 7u, 0, 0);
            auto us = addPlayer(sim);
            auto them = addPlayer(sim);
            defineRespawnCommander(sim);
            auto com = sim.trySpawnCompletedUnit("ARMCOM", us, SimVector(-200_ss, 0_ss, 0_ss), std::nullopt);
            auto other = sim.trySpawnCompletedUnit("ARMCOM", us, SimVector(-100_ss, 0_ss, 0_ss), std::nullopt);
            auto enemy = sim.trySpawnCompletedUnit("ARMCOM", them, SimVector(200_ss, 0_ss, 0_ss), std::nullopt);
            REQUIRE(com);
            REQUIRE(other);
            REQUIRE(enemy);
            sim.unitDefinitions["ARMCOM"].energyStorage = Energy(100000);
            sim.commanderDeathMode = mode;
            auto& player = sim.getPlayer(us);
            player.startingMetal = Metal(100);
            player.startingEnergy = Energy(150);
            player.metal = Metal(37);
            player.energy = Energy(51);
            sim.selfDestructUnit(*com);
            sim.tick();
            if (mode == CommanderDeathMode::GameContinues)
            {
                CHECK(sim.tryGetUnitState(*other));
                CHECK(player.status == GamePlayerStatus::Alive);
                continue;
            }
            CHECK_FALSE(sim.tryGetUnitState(*other));
            CHECK(player.status == GamePlayerStatus::Dead);
            tick(sim, 178);
            CHECK(player.respawnCount == 0);
            CHECK(player.defeatCountdown == 0);
            sim.tick();
            if (mode == CommanderDeathMode::GameEnds)
            {
                CHECK(player.status == GamePlayerStatus::Dead);
                CHECK(player.defeatCountdown == -2);
            }
            else
            {
                CHECK(player.status == GamePlayerStatus::Alive);
                CHECK(player.respawnCount == 1);
                CHECK(player.defeatCountdown == -1);
                CHECK(player.hasBaseStorage);
                CHECK(player.maxMetal == Metal(1200));
                CHECK(player.metal == Metal(137));
                CHECK(player.energy == Energy(201));
                CHECK(std::holds_alternative<WinStatusUndecided>(sim.decidedStatus()));
                CHECK(std::holds_alternative<WinStatusUndecided>(sim.computeWinStatus()));
                // A second death must start a fresh full wait.
                sim.killPlayer(us);
                tick(sim, 179);
                CHECK(player.respawnCount == 1);
                sim.tick();
                CHECK(player.respawnCount == 2);
            }
        }
    }

    TEST_CASE("Deathmatch state survives a save and dropped seats never respawn", "[deathmatch][saveload]")
    {
        GameSimulation sim(makeFlatTerrain(128, 128), 7u, 0, 0);
        auto us = addPlayer(sim);
        addPlayer(sim);
        defineRespawnCommander(sim);
        sim.commanderDeathMode = CommanderDeathMode::Deathmatch;
        auto& player = sim.getPlayer(us);
        player.commanderType = "ARMCOM";
        player.defeatCountdown = 2;
        player.respawnCount = 3;
        player.status = GamePlayerStatus::Dead;
        player.removedFromGame = true;
        const auto saved = saveSimulationToJson(sim);
        auto restored = GameSimulation(makeFlatTerrain(128, 128), 0u, 0, 0);
        restored.unitDefinitions = sim.unitDefinitions;
        restored.unitModelDefinitions = sim.unitModelDefinitions;
        restored.unitScriptDefinitions = sim.unitScriptDefinitions;
        loadSimulationFromJson(saved, restored);
        CHECK(computeHashOf(sim) == computeHashOf(restored));
        tick(restored, 210);
        CHECK(restored.getPlayer(us).respawnCount == 3);
        CHECK(restored.getPlayer(us).status == GamePlayerStatus::Dead);
    }

    TEST_CASE("an eliminated seat finishes its countdown while opponents keep playing", "[spectators][endgame]")
    {
        GameSimulation sim(makeFlatTerrain(128, 128), 7u, 0, 0);
        defineRespawnCommander(sim);
        for (int i = 0; i < 3; ++i)
        {
            auto player = addPlayer(sim);
            REQUIRE(sim.trySpawnCompletedUnit("ARMCOM", player, SimVector(intToSimScalar(i * 200 - 200), 0_ss, 0_ss), std::nullopt));
        }
        sim.killPlayer(PlayerId(0));
        tick(sim, 179);
        CHECK(sim.getPlayer(PlayerId(0)).defeatCountdown == 0);
        sim.tick();
        CHECK(sim.getPlayer(PlayerId(0)).defeatCountdown == -2);
        CHECK(std::holds_alternative<WinStatusUndecided>(sim.decidedStatus()));
        CHECK(sim.getPlayer(PlayerId(1)).status == GamePlayerStatus::Alive);
        CHECK(sim.getPlayer(PlayerId(2)).status == GamePlayerStatus::Alive);
    }

    TEST_CASE("ordinary multiplayer ends when every human is out even with AI units left", "[spectators][endgame]")
    {
        GameSimulation sim(makeFlatTerrain(128, 128), 7u, 0, 0);
        defineRespawnCommander(sim);
        auto human = addPlayer(sim);
        auto computer = addPlayer(sim);
        sim.getPlayer(computer).type = GamePlayerType::Computer;
        REQUIRE(sim.trySpawnCompletedUnit("ARMCOM", human, SimVector(-200_ss, 0_ss, 0_ss), std::nullopt));
        REQUIRE(sim.trySpawnCompletedUnit("ARMCOM", computer, SimVector(200_ss, 0_ss, 0_ss), std::nullopt));
        sim.killPlayer(human);
        CHECK(std::holds_alternative<WinStatusNoSurvivors>(sim.computeWinStatus()));
        sim.commanderDeathMode = CommanderDeathMode::Deathmatch;
        CHECK(std::holds_alternative<WinStatusUndecided>(sim.computeWinStatus()));
    }

    TEST_CASE("losing a survivor of the commander wipe still defeats the player", "[deathmatch][endgame]")
    {
        for (const auto mode : {CommanderDeathMode::GameEnds, CommanderDeathMode::Deathmatch})
        {
            GameSimulation sim(makeFlatTerrain(128, 128), 7u, 0, 0);
            auto us = addPlayer(sim);
            addPlayer(sim);
            defineRespawnCommander(sim);
            auto heavy = sim.unitDefinitions.at("ARMCOM");
            heavy.commander = false;
            heavy.maxHitPoints = 50000;
            sim.unitDefinitions["HEAVY"] = heavy;
            sim.unitScriptDefinitions["HEAVY"] = *makeEmptyCobScript();
            auto com = sim.trySpawnCompletedUnit("ARMCOM", us, SimVector(-200_ss, 0_ss, 0_ss), std::nullopt);
            auto survivor = sim.trySpawnCompletedUnit("HEAVY", us, SimVector(200_ss, 0_ss, 0_ss), std::nullopt);
            REQUIRE(com);
            REQUIRE(survivor);
            sim.getUnitState(*survivor).kills = 25;
            sim.commanderDeathMode = mode;
            sim.selfDestructUnit(*com);
            sim.tick();
            CHECK(sim.getPlayer(us).status == GamePlayerStatus::Alive);
            CHECK(sim.getUnitState(*survivor).hitPoints == 26000);
            sim.killUnit(*survivor);
            sim.tick();
            CHECK(sim.getPlayer(us).status == GamePlayerStatus::Dead);
        }
    }

    TEST_CASE("a commander killed by an army-wipe blast triggers its own army wipe", "[deathmatch][endgame]")
    {
        GameSimulation sim(makeFlatTerrain(128, 128), 7u, 0, 0);
        auto us = addPlayer(sim);
        auto them = addPlayer(sim);
        defineRespawnCommander(sim);
        auto ordinary = sim.unitDefinitions.at("ARMCOM");
        ordinary.commander = false;
        ordinary.maxHitPoints = 100;
        sim.unitDefinitions["GUARD"] = ordinary;
        sim.unitScriptDefinitions["GUARD"] = *makeEmptyCobScript();
        ordinary.selfDestructAs = "BLAST";
        sim.unitDefinitions["BOMB"] = ordinary;
        sim.unitScriptDefinitions["BOMB"] = *makeEmptyCobScript();
        WeaponDefinition blast{};
        blast.damageRadius = 500_ss;
        blast.damage["DEFAULT"] = 10000;
        sim.weaponDefinitions["BLAST"] = blast;
        auto first = sim.trySpawnCompletedUnit("ARMCOM", us, SimVector(-800_ss, 0_ss, 0_ss), std::nullopt);
        REQUIRE(first);
        REQUIRE(sim.trySpawnCompletedUnit("BOMB", us, SimVector(0_ss, 0_ss, 0_ss), std::nullopt));
        REQUIRE(sim.trySpawnCompletedUnit("ARMCOM", them, SimVector(40_ss, 0_ss, 0_ss), std::nullopt));
        auto distant = sim.trySpawnCompletedUnit("GUARD", them, SimVector(800_ss, 0_ss, 0_ss), std::nullopt);
        REQUIRE(distant);
        sim.selfDestructUnit(*first);
        sim.processVictoryCondition();
        CHECK(sim.getUnitState(*distant).isDead());
        CHECK(sim.getPlayer(them).status == GamePlayerStatus::Dead);
    }
}
