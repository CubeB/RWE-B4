#include <catch2/catch_test_macros.hpp>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/save_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        GameSimulation makeDropSimulation()
        {
            GameSimulation sim(makeFlatTerrain(128, 128), 0u, 100, 3000);
            // Shipped totala1.hpi, units/ARMSOLAR.FBI and weapons/UNITS.TDF.
            // Only the model and COB are inert stand-ins. BT-232 concerns the
            // owner wipe, independent of the unit's script or build progress.
            UnitDefinition solar{};
            solar.objectName = "ARMSOLAR";
            solar.maxHitPoints = 326;
            solar.buildTime = 2495;
            solar.sightDistance = 210;
            solar.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5, 5, 10, 0, 0, 0};
            solar.yardMap = Grid<YardMapCell>(5, 5, YardMapCell::Ground);
            solar.explodeAs = "SMALL_BUILDINGEX";
            solar.selfDestructAs = "SMALL_BUILDING";
            sim.unitDefinitions["ARMSOLAR"] = solar;
            sim.unitModelDefinitions["ARMSOLAR"] = createUnitModelDefinition(10_ss, {});
            sim.unitScriptDefinitions["ARMSOLAR"] = *makeEmptyCobScript();
            WeaponDefinition blast{};
            blast.physicsType = ProjectilePhysicsTypeBallistic();
            blast.maxRange = 480_ss;
            blast.velocity = 250_ss / 30_ss;
            blast.damageRadius = SimScalar(102.5f); // AreaOfEffect=205 / 2.
            blast.damage["DEFAULT"] = 900;
            sim.weaponDefinitions["SMALL_BUILDING"] = blast;
            blast.velocity = 70_ss / 30_ss;
            blast.damage["DEFAULT"] = 40;
            sim.weaponDefinitions["SMALL_BUILDINGEX"] = blast;
            return sim;
        }

        UnitId addSolar(GameSimulation& sim, PlayerId owner, SimVector position)
        {
            auto id = sim.trySpawnCompletedUnit("ARMSOLAR", owner, position, std::nullopt);
            REQUIRE(id);
            return *id;
        }

        void registerDropPeers(PlayerCommandService& service)
        {
            for (unsigned int i = 0; i < 2; ++i)
            {
                service.registerPlayer(PlayerId(i));
                service.registerHashSource(PlayerId(i));
            }
        }
    }

    TEST_CASE("BT-232 removes a dropped army on the agreed tick", "[network][playerdrop]")
    {
        auto sim = makeDropSimulation();
        auto survivor = addPlayer(sim);
        auto dropped = addPlayer(sim);
        auto safe = addSolar(sim, survivor, SimVector(-400_ss, 0_ss, -400_ss));
        auto finished = addSolar(sim, dropped, SimVector(0_ss, 0_ss, 0_ss));
        auto frame = addSolar(sim, dropped, SimVector(400_ss, 0_ss, 400_ss));
        sim.getUnitState(frame).buildTimeCompleted = 1;

        PlayerCommandService service;
        registerDropPeers(service);
        const PlayerDroppedCommand command{dropped, 3};
        service.pushCommands(survivor, {command});

        // Packet receipt closes the stream but cannot kill anything.
        REQUIRE(sim.getUnitState(finished).isAlive());
        REQUIRE_FALSE(sim.getPlayer(dropped).removeUnitsAt);
        REQUIRE(service.tryPopCommands());
        applyPlayerDropToSimulation(sim, command, 1);
        REQUIRE(sim.getPlayer(dropped).removeUnitsAt == GameTime(3));
        sim.tick();
        sim.tick();
        REQUIRE(sim.tryGetUnitState(finished));
        REQUIRE(sim.tryGetUnitState(frame));
        REQUIRE(sim.getPlayer(dropped).status == GamePlayerStatus::Alive);
        sim.tick();
        REQUIRE_FALSE(sim.tryGetUnitState(finished));
        REQUIRE_FALSE(sim.tryGetUnitState(frame));
        REQUIRE(sim.getUnitState(safe).isAlive());
        REQUIRE(sim.getPlayer(dropped).status == GamePlayerStatus::Dead);
        REQUIRE(sim.getPlayer(dropped).unitsLost == 2);
        REQUIRE(sim.getPlayer(survivor).unitsKilled == 0);
        REQUIRE_FALSE(sim.getPlayer(dropped).removeUnitsAt);
        // A repeated announcement must not schedule another wipe.
        applyPlayerDropToSimulation(sim, command, 4);
        REQUIRE_FALSE(sim.getPlayer(dropped).removeUnitsAt);
    }

    TEST_CASE("BT-232 preserves a pending wipe across a replay keyframe", "[network][playerdrop][saveload]")
    {
        auto sim = makeDropSimulation();
        auto survivor = addPlayer(sim);
        auto dropped = addPlayer(sim);
        addSolar(sim, survivor, SimVector(-400_ss, 0_ss, -400_ss));
        auto victim = addSolar(sim, dropped, SimVector(400_ss, 0_ss, 400_ss));
        // A loaded game's simulation clock does not restart with scene time.
        sim.gameTime = GameTime(600);
        PlayerCommandService service;
        registerDropPeers(service);
        const PlayerDroppedCommand command{dropped, 3};
        auto before = computeHashOf(sim);
        PlayerCommandService rejected;
        registerDropPeers(rejected);
        rejected.pushCommands(dropped, {command});
        rejected.pushCommands(survivor, {});
        auto refused = rejected.tryPopCommands();
        REQUIRE(refused);
        for (const auto& [issuer, commands] : *refused)
        {
            REQUIRE(commands.empty());
        }
        REQUIRE(computeHashOf(sim) == before);
        service.pushCommands(survivor, {command});
        // A future rejoin arriving early cannot cancel an accepted wipe.
        service.pushCommands(survivor, {PlayerRejoinedCommand{dropped, 4}});
        REQUIRE_FALSE(service.isDropped(dropped));
        auto accepted = service.tryPopCommands();
        REQUIRE(accepted);
        unsigned int dropCount = 0;
        for (const auto& [issuer, commands] : *accepted)
        {
            for (const auto& c : commands)
            {
                if (const auto* drop = std::get_if<PlayerDroppedCommand>(&c))
                {
                    applyPlayerDropToSimulation(sim, *drop, 1);
                    ++dropCount;
                }
            }
        }
        REQUIRE(dropCount == 1);
        REQUIRE(computeHashOf(sim) != before);
        REQUIRE(sim.getPlayer(dropped).removeUnitsAt == GameTime(603));
        // A duplicate on a later tick must not move the deadline.
        applyPlayerDropToSimulation(sim, command, 2);
        REQUIRE(sim.getPlayer(dropped).removeUnitsAt == GameTime(603));
        sim.tick();
        auto saved = saveSimulationToJson(sim);
        auto restored = makeDropSimulation();
        loadSimulationFromJson(saved, restored);
        REQUIRE(saveSimulationToJson(restored) == saved);
        REQUIRE(dumpJson(restored.getPlayer(dropped))["removeUnitsAt"] == dumpJson(GameTime(603)));
        for (int i = 0; i < 3; ++i)
        {
            sim.tick();
            restored.tick();
            REQUIRE(computeHashOf(sim) == computeHashOf(restored));
        }
        REQUIRE_FALSE(restored.tryGetUnitState(victim));
        REQUIRE(restored.getPlayer(dropped).unitsLost == 1);
    }
}
