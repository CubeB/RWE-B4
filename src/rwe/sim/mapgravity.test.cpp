#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/game/save_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/ProjectilePhysicsType.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    using Catch::Approx;

    namespace
    {
        MapTerrain makeWideTerrain()
        {
            Grid<unsigned char> heights(512, 512, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void defineGun(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeBallistic();
            w.maxRange = 2000_ss;
            w.velocity = 10_ss;
            w.burst = 1;
            w.damageRadius = 8_ss;
            w.damage["DEFAULT"] = 10;
            sim.weaponDefinitions["gun"] = w;
        }

        /** The height of a level shot, fired from 1000 up, after `ticks` ticks on a map of the given gravity. */
        float heightAfter(int gravity, int ticks)
        {
            GameSimulation sim(makeWideTerrain(), 0u, 0, 0);
            sim.gravity = gravity;
            auto us = addPlayer(sim, "us");
            defineGun(sim);

            UnitWeapon weapon;
            weapon.weaponType = "gun";
            sim.spawnProjectile(ProjectileSpawn{
                .owner = us,
                .weapon = &weapon,
                .position = SimVector(0_ss, 1000_ss, 0_ss),
                .direction = SimVector(1_ss, 0_ss, 0_ss),
                .distanceToTarget = 500_ss,
                .targetUnit = std::nullopt,
                .attacker = std::nullopt});
            tick(sim, ticks);
            REQUIRE(sim.projectiles.begin() != sim.projectiles.end());
            return simScalarToFloat(sim.projectiles.begin()->second.position.y);
        }
    }

    TEST_CASE("the OTA gravity key is read as written, and a missing one is zero", "[ota][gravity]")
    {
        // BT-124: the reader at 0x436542 stores the integer with a default of
        // zero. 150 is a real shipped value (one of the 83 maps that are not 112).
        auto withKey = parseOta(parseTdfFromString(R"TDF(
[GlobalHeader]
    {
    missionname=Gravity Test;
    gravity=150;
    SCHEMACOUNT=0;
    }
)TDF"));
        REQUIRE(withKey.gravity == 150);

        auto withoutKey = parseOta(parseTdfFromString(R"TDF(
[GlobalHeader]
    {
    missionname=Gravity Test;
    SCHEMACOUNT=0;
    }
)TDF"));
        REQUIRE(withoutKey.gravity == 0);
    }

    TEST_CASE("a simulation with no map keeps the usual 112", "[gravity]")
    {
        GameSimulation sim(makeWideTerrain(), 0u, 0, 0);
        REQUIRE(sim.gravity == 112);
        REQUIRE(simScalarToFloat(sim.gravityPerTick()) == Approx(112.0f / 900.0f));
    }

    TEST_CASE("a ballistic shell falls at the map's own gravity", "[gravity][projectile]")
    {
        // Gravity comes off the velocity before the position moves, so the
        // drop after N ticks is g * N * (N + 1) / 2 per tick squared.
        constexpr int Ticks = 40;
        auto drop = [](int gravity) { return 1000.0f - heightAfter(gravity, Ticks); };
        auto expected = [](int gravity) { return (static_cast<float>(gravity) / 900.0f) * Ticks * (Ticks + 1) / 2.0f; };

        SECTION("112 is what it always was")
        {
            REQUIRE(drop(112) == Approx(expected(112)).epsilon(0.001));
        }

        SECTION("150 drops a third again as far")
        {
            REQUIRE(drop(150) == Approx(expected(150)).epsilon(0.001));
            REQUIRE(drop(150) > drop(112) * 1.3f);
        }

        SECTION("20 barely drops it")
        {
            REQUIRE(drop(20) == Approx(expected(20)).epsilon(0.002));
        }

        SECTION("none at all flies it level")
        {
            REQUIRE(heightAfter(0, Ticks) == Approx(1000.0f));
        }
    }

    TEST_CASE("the aim solver's pitch follows the map's gravity", "[gravity][targeting]")
    {
        auto lowPitch = [](int gravity) {
            GameSimulation sim(makeWideTerrain(), 0u, 0, 0);
            sim.gravity = gravity;
            auto angles = computeFiringAngles(300_ss / 30_ss, sim.gravityPerTick(), 400_ss, 0_ss);
            REQUIRE(angles.has_value());
            return std::min(toRadians(angles->first).value, toRadians(angles->second).value);
        };

        // Heavier gravity needs a steeper lob to cover the same ground; less
        // of it a flatter one.
        REQUIRE(lowPitch(150) > lowPitch(112));
        REQUIRE(lowPitch(112) > lowPitch(20));
        REQUIRE(lowPitch(20) > 0.0f);
    }

    TEST_CASE("on a map with no gravity the only arc is the straight line", "[gravity][targeting]")
    {
        auto angles = computeFiringAngles(300_ss / 30_ss, 0_ss, 400_ss, 100_ss);
        REQUIRE(angles.has_value());
        REQUIRE(toRadians(angles->first).value == Approx(std::atan2(100.0f, 400.0f)).epsilon(0.001));
        REQUIRE(angles->first == angles->second);
    }

    TEST_CASE("a bomber's release lead shrinks as gravity grows", "[gravity][bomber]")
    {
        UnitDefinition bomber{};
        bomber.cruiseAltitude = 200_ss;
        bomber.attackRunLength = 0_ss;
        auto lead = [&](int gravity) {
            GameSimulation sim(makeWideTerrain(), 0u, 0, 0);
            sim.gravity = gravity;
            return simScalarToFloat(bombReleaseTrigger(bomber, 5_ss, sim.gravityPerTick()));
        };
        REQUIRE(lead(20) > lead(112));
        REQUIRE(lead(112) > lead(150));
        REQUIRE(lead(0) == Approx(1.0f));
    }

    TEST_CASE("the map's gravity survives a save", "[gravity][saveload]")
    {
        GameSimulation a(makeWideTerrain(), 0u, 0, 0);
        a.gravity = 150;
        auto saved = saveSimulationToJson(a);

        GameSimulation b(makeWideTerrain(), 0u, 0, 0);
        REQUIRE(b.gravity == 112);
        loadSimulationFromJson(saved, b);
        REQUIRE(b.gravity == 150);
    }

    TEST_CASE("a save from before the key was read falls at 112", "[gravity][saveload]")
    {
        GameSimulation a(makeWideTerrain(), 0u, 0, 0);
        auto saved = saveSimulationToJson(a);
        saved.erase("gravity");

        GameSimulation b(makeWideTerrain(), 0u, 0, 0);
        loadSimulationFromJson(saved, b);
        REQUIRE(b.gravity == 112);
    }
}
