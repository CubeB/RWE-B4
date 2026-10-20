#include <catch2/catch_test_macros.hpp>
#include <rwe/game/save_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
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
}
