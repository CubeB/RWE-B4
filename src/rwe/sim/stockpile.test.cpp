#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeStockpileTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addStockpilePlayer(GameSimulation& sim, const std::string& name, float metal, float energy)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(metal),
                Energy(energy),
                Metal(1000000.0f),
                Energy(1000000.0f),
                Metal(metal),
                Energy(energy),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeStockpileScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerStockpileModel(GameSimulation& sim)
        {
            // Twenty units up, because the piece doubles as the muzzle: a shot
            // spawned at ground level buries itself on the tick it is created.
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        void defineStockpileUnit(GameSimulation& sim, const std::string& type, bool canAttack)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = canAttack;
            d.sightDistance = 1000u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL1 WEAPON NOTAIR NOTSUB";
            // Storage is recomputed from the units on the field every second,
            // so without it the player's stores would be clamped to nothing.
            d.metalStorage = Metal(1000000.0f);
            d.energyStorage = Energy(1000000.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        /** A launcher's missile: costly, slow to build, and useless until one is ready. */
        void defineStockpileWeapon(GameSimulation& sim, const std::string& name, float reloadSeconds, float metal, float energy)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 400_ss;
            w.reloadTime = SimScalar(reloadSeconds);
            w.burst = 1;
            w.velocity = 450_ss / 30_ss;
            w.damageRadius = 4_ss;
            w.damage["DEFAULT"] = 1;
            w.tolerance = SimAngle(8000u);
            w.pitchTolerance = SimAngle(8000u);
            w.metalPerShot = Metal(metal);
            w.energyPerShot = Energy(energy);
            w.stockpile = true;
            sim.weaponDefinitions[name] = w;
        }

        UnitId spawnStockpileUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        void armWithStockpileWeapon(GameSimulation& sim, UnitId id, const std::string& weaponType)
        {
            UnitWeapon weapon;
            weapon.weaponType = weaponType;
            sim.getUnitState(id).weapons[0] = weapon;
        }

        const UnitWeapon& weaponOf(const GameSimulation& sim, UnitId id)
        {
            return *sim.getUnitState(id).weapons[0];
        }

        void tick(GameSimulation& sim, int ticks)
        {
            for (int i = 0; i < ticks; ++i)
            {
                sim.tick();
            }
        }

        bool anyProjectiles(const GameSimulation& sim)
        {
            for ([[maybe_unused]] const auto& p : sim.projectiles)
            {
                return true;
            }
            return false;
        }

        bool everFires(GameSimulation& sim, int ticks)
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

        /** What the whole build of one round costs, stepped exactly as the sim steps it. */
        long long rampTotal(int totalTicks, float cost)
        {
            long long spent = 0;
            for (int p = 0; p < totalTicks; p += StockpileStepTicks)
            {
                auto q = std::min(p + StockpileStepTicks, totalTicks);
                spent += stockpileRampTotal(q, totalTicks, cost) - stockpileRampTotal(p, totalTicks, cost);
            }
            return spent;
        }
    }

    TEST_CASE("a stockpiled round is paid for as it is built", "[stockpile]")
    {
        // TotalA.exe 0x402BD4. The round takes the weapon's own reloadtime to
        // build and the cost is a straight ramp across that, charged one tick
        // in five. Two seconds and 200 metal here stands in for the three
        // minutes and 2000 metal a nuclear missile really wants.
        auto script = makeStockpileScript();
        GameSimulation sim(makeStockpileTerrain(), 0u, 0, 0);
        auto us = addStockpilePlayer(sim, "us", 100000.0f, 100000.0f);
        registerStockpileModel(sim);
        defineStockpileUnit(sim, "silo", true);
        defineStockpileWeapon(sim, "missile", 2.0f, 200.0f, 1000.0f);

        auto siloId = spawnStockpileUnit(sim, "silo", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWithStockpileWeapon(sim, siloId, "missile");

        SECTION("nothing is built and nothing is spent until a round is ordered")
        {
            auto metalBefore = sim.getPlayer(us).metal;
            tick(sim, 90);
            REQUIRE(weaponOf(sim, siloId).stockedRounds == 0);
            REQUIRE(sim.getPlayer(us).metal == metalBefore);
        }

        SECTION("one ordered round takes the reload time and costs exactly the TDF numbers")
        {
            auto metalBefore = sim.getPlayer(us).metal;
            auto energyBefore = sim.getPlayer(us).energy;
            sim.modifyStockpileQueue(siloId, 1);

            // Half way through it is under way but not finished.
            tick(sim, 30);
            REQUIRE(weaponOf(sim, siloId).stockedRounds == 0);
            REQUIRE(weaponOf(sim, siloId).stockpileProgress > 0);

            // Two seconds is sixty ticks, and the round lands on the last one.
            tick(sim, 30);
            REQUIRE(weaponOf(sim, siloId).stockedRounds == 1);
            REQUIRE(weaponOf(sim, siloId).queuedRounds == 0);

            // Let the second settle so the spend shows in the stores, then
            // check nothing further is taken once the queue is empty.
            tick(sim, 60);
            REQUIRE(weaponOf(sim, siloId).stockedRounds == 1);
            REQUIRE((metalBefore - sim.getPlayer(us).metal).value == 200.0f);
            REQUIRE((energyBefore - sim.getPlayer(us).energy).value == 1000.0f);
        }

        SECTION("two ordered rounds take twice as long and cost twice as much")
        {
            auto metalBefore = sim.getPlayer(us).metal;
            sim.modifyStockpileQueue(siloId, 2);

            tick(sim, 60);
            REQUIRE(weaponOf(sim, siloId).stockedRounds == 1);
            REQUIRE(weaponOf(sim, siloId).queuedRounds == 1);

            tick(sim, 60);
            REQUIRE(weaponOf(sim, siloId).stockedRounds == 2);
            REQUIRE(weaponOf(sim, siloId).queuedRounds == 0);

            tick(sim, 30);
            REQUIRE((metalBefore - sim.getPlayer(us).metal).value == 400.0f);
        }

        SECTION("cancelling the last round throws away the part-built one")
        {
            sim.modifyStockpileQueue(siloId, 1);
            tick(sim, 30);
            REQUIRE(weaponOf(sim, siloId).stockpileProgress > 0);

            sim.modifyStockpileQueue(siloId, -1);
            REQUIRE(weaponOf(sim, siloId).queuedRounds == 0);
            REQUIRE(weaponOf(sim, siloId).stockpileProgress == 0);
        }
    }

    TEST_CASE("a launcher that cannot pay builds nothing rather than going into debt", "[stockpile]")
    {
        // 0x402C9C: the resource call fails, the progress is not advanced and
        // the order comes back in ten ticks instead of five. A silo on a dead
        // economy sits at the same point in the build for as long as it takes.
        auto script = makeStockpileScript();
        GameSimulation sim(makeStockpileTerrain(), 0u, 0, 0);
        auto us = addStockpilePlayer(sim, "us", 0.0f, 0.0f);
        registerStockpileModel(sim);
        defineStockpileUnit(sim, "silo", true);
        defineStockpileWeapon(sim, "missile", 2.0f, 200.0f, 1000.0f);

        auto siloId = spawnStockpileUnit(sim, "silo", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWithStockpileWeapon(sim, siloId, "missile");
        sim.modifyStockpileQueue(siloId, 1);

        tick(sim, 200);
        REQUIRE(weaponOf(sim, siloId).stockedRounds == 0);
        REQUIRE(weaponOf(sim, siloId).stockpileProgress == 0);
        REQUIRE(weaponOf(sim, siloId).queuedRounds == 1);

        // Pay the bill and the same order finishes.
        sim.getPlayer(us).metal = Metal(100000.0f);
        sim.getPlayer(us).energy = Energy(100000.0f);
        tick(sim, 90);
        REQUIRE(weaponOf(sim, siloId).stockedRounds == 1);
    }

    TEST_CASE("firing spends a round out of the magazine and nothing out of the economy", "[stockpile]")
    {
        // 0x49E3D5 asks a stockpiled weapon for a round instead of asking the
        // player for metal, 0x49E447 takes it, and 0x49E512 skips the spend
        // that every other weapon makes. The launcher was paid for in advance.
        auto script = makeStockpileScript();
        GameSimulation sim(makeStockpileTerrain(), 0u, 0, 0);
        auto us = addStockpilePlayer(sim, "us", 100000.0f, 100000.0f);
        auto them = addStockpilePlayer(sim, "them", 100000.0f, 100000.0f);
        registerStockpileModel(sim);
        defineStockpileUnit(sim, "silo", true);
        defineStockpileUnit(sim, "victim", false);
        defineStockpileWeapon(sim, "missile", 2.0f, 200.0f, 1000.0f);

        auto siloId = spawnStockpileUnit(sim, "silo", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWithStockpileWeapon(sim, siloId, "missile");
        spawnStockpileUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 128_ss), script);

        SECTION("an empty magazine holds its fire with a target in front of it")
        {
            REQUIRE(!everFires(sim, 60));
        }

        SECTION("a round in the magazine is spent on the shot, and the stores are untouched")
        {
            sim.getUnitState(siloId).weapons[0]->stockedRounds = 1;
            auto metalBefore = sim.getPlayer(us).metal;
            auto energyBefore = sim.getPlayer(us).energy;

            REQUIRE(everFires(sim, 60));
            REQUIRE(weaponOf(sim, siloId).stockedRounds == 0);

            tick(sim, 30);
            REQUIRE(sim.getPlayer(us).metal == metalBefore);
            REQUIRE(sim.getPlayer(us).energy == energyBefore);
        }
    }

    TEST_CASE("a magazine holds two hundred rounds and no more", "[stockpile]")
    {
        // 0x402CCA compares the count against 0xC8 and, when it is full, waits
        // three hundred ticks rather than dropping the order.
        auto script = makeStockpileScript();
        GameSimulation sim(makeStockpileTerrain(), 0u, 0, 0);
        auto us = addStockpilePlayer(sim, "us", 100000.0f, 100000.0f);
        registerStockpileModel(sim);
        defineStockpileUnit(sim, "silo", true);
        defineStockpileWeapon(sim, "missile", 2.0f, 200.0f, 1000.0f);

        auto siloId = spawnStockpileUnit(sim, "silo", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWithStockpileWeapon(sim, siloId, "missile");
        sim.getUnitState(siloId).weapons[0]->stockedRounds = MaxStockedRounds;
        sim.modifyStockpileQueue(siloId, 5);

        auto metalBefore = sim.getPlayer(us).metal;
        tick(sim, 120);

        REQUIRE(weaponOf(sim, siloId).stockedRounds == MaxStockedRounds);
        REQUIRE(weaponOf(sim, siloId).queuedRounds == 5);
        REQUIRE(sim.getPlayer(us).metal == metalBefore);
    }

    TEST_CASE("the build ramp comes to exactly what the weapon TDF asked for", "[stockpile]")
    {
        // The point of truncating the running total at each end rather than
        // truncating the step (0x402C0F, 0x402C20) is that the sum lands on the
        // TDF number however awkwardly the cost divides by the tick count.
        // These are the shipped figures, replayed the way the sim steps them.

        SECTION("a nuclear missile: three minutes, 2000 metal, 180000 energy")
        {
            REQUIRE(rampTotal(180 * 30, 2000.0f) == 2000);
            REQUIRE(rampTotal(180 * 30, 180000.0f) == 180000);
        }

        SECTION("an anti-nuke: two minutes, 200 metal, 10000 energy")
        {
            REQUIRE(rampTotal(120 * 30, 200.0f) == 200);
            REQUIRE(rampTotal(120 * 30, 10000.0f) == 10000);
        }

        SECTION("a cost that divides badly still comes to the whole of itself")
        {
            REQUIRE(rampTotal(7 * 30, 333.0f) == 333);
            REQUIRE(rampTotal(7 * 30, 7.0f) == 7);
        }

        SECTION("a cost smaller than the number of steps is not lost to rounding")
        {
            REQUIRE(rampTotal(60 * 30, 3.0f) == 3);
            REQUIRE(rampTotal(60 * 30, 1.0f) == 1);
        }

        SECTION("nothing is charged for a free round")
        {
            REQUIRE(rampTotal(60 * 30, 0.0f) == 0);
        }
    }
}
