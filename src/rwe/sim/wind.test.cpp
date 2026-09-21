#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/ProjectilePhysicsType.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <cmath>
#include <string>

namespace rwe
{
    using Catch::Approx;

    namespace
    {
        /**
         * 512 cells of 16 world units, so the map spans 8192 and -- since
         * worldToHeightmapSpace offsets by half the width -- the world origin
         * is its centre. A round fired from the origin has 4096 units of room
         * in every direction before anything culls it for leaving the map.
         */
        MapTerrain makeWideTerrain()
        {
            Grid<unsigned char> heights(512, 512, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * Brain Coral's maxwindspeed, and the speed every hand-worked figure
         * below is taken at. The original stores 16.16 fixed point, so the
         * per-tick displacement is 2 * 3000 / 65536.
         */
        constexpr int BrainCoralWindSpeed = 3000;
        constexpr float BrainCoralDrift = 0.0915527344f;

        /** Crystal Isles, and the ceiling a generator can use (MaxUtilizableWindSpeed). */
        constexpr int CrystalIslesWindSpeed = 5000;
        constexpr float CrystalIslesDrift = 0.152587891f;

        /** The ballistic gravity from updateProjectiles, per tick squared. */
        constexpr float GravityPerTickSquared = 112.0f / 900.0f;

        /**
         * A gun with the given physics and nothing else to it.
         *
         * weaponTimer is deliberately left unset. For a ballistic weapon
         * createProjectileFromWeapon skips the line-of-sight branch and falls
         * to `else if (weaponDefinition.weaponTimer)`, which dereferences
         * randomDecay without checking it -- so a weapon carrying a timer but
         * no decay throws on a bad optional access. Leaving both empty also
         * leaves dieOnFrame unset, which is what keeps the round alive for the
         * whole flight below.
         */
        void defineTestGun(GameSimulation& sim, const std::string& name, const ProjectilePhysicsType& physics)
        {
            WeaponDefinition w{};
            w.physicsType = physics;
            w.maxRange = 2000_ss;
            w.velocity = 10_ss;
            w.burst = 1;
            w.damageRadius = 8_ss;
            w.damage["DEFAULT"] = 10;
            sim.weaponDefinitions[name] = w;
        }
    }

    TEST_CASE("computeWindVector: the components the original actually stores", "[wind]")
    {
        // 0x490CA4-0x490D35. The point of pinning the cardinals is that they
        // separate sin from cos: docs/TOTALA-EXE.md had the two the wrong way
        // round, and against a uniformly random direction no amount of play
        // would ever have shown it -- both conventions give the same
        // distribution. Only a fixed direction tells them apart.

        SECTION("direction zero blows along -Z, because cos leads there")
        {
            auto w = computeWindVector(SimAngle(0), BrainCoralWindSpeed);
            REQUIRE(simScalarToFloat(w.x) == Approx(0.0f).margin(1e-6f));
            REQUIRE(simScalarToFloat(w.z) == Approx(-BrainCoralDrift));
        }

        SECTION("a quarter turn blows along -X, which is the sin term")
        {
            // If the table at 0x509f00 were a cosine table -- the reading this
            // replaces -- these two would be exchanged.
            auto w = computeWindVector(SimAngle(16384), BrainCoralWindSpeed);
            REQUIRE(simScalarToFloat(w.x) == Approx(-BrainCoralDrift));
            REQUIRE(simScalarToFloat(w.z) == Approx(0.0f).margin(1e-6f));
        }

        SECTION("half a turn reverses the zero case")
        {
            auto w = computeWindVector(SimAngle(32768), BrainCoralWindSpeed);
            REQUIRE(simScalarToFloat(w.x) == Approx(0.0f).margin(1e-6f));
            REQUIRE(simScalarToFloat(w.z) == Approx(BrainCoralDrift));
        }
    }

    TEST_CASE("computeWindVector: the wind is strictly horizontal", "[wind]")
    {
        // globals+0x37ED0, the vector's Y word, is never written anywhere in
        // the binary. Wind moves a shell downrange and sideways; it never
        // holds one up or presses it down.
        for (int raw = 0; raw < 65536; raw += 2731)
        {
            auto w = computeWindVector(SimAngle(static_cast<uint16_t>(raw)), CrystalIslesWindSpeed);
            REQUIRE(simScalarToFloat(w.y) == 0.0f);
        }
    }

    TEST_CASE("computeWindVector: speed scales the drift, and stillness means stillness", "[wind]")
    {
        SECTION("the magnitude is two raw speed units a tick, converted out of 16.16")
        {
            auto w = computeWindVector(SimAngle(16384), CrystalIslesWindSpeed);
            REQUIRE(simScalarToFloat(w.x) == Approx(-CrystalIslesDrift));
        }

        SECTION("a becalmed map gets no wind at all")
        {
            // Every map with minwindspeed=maxwindspeed=0 -- ota.test.cpp has
            // one -- must leave projectiles exactly on their old arc, or the
            // port would have changed behaviour on maps that have no wind.
            for (int raw = 0; raw < 65536; raw += 5461)
            {
                auto w = computeWindVector(SimAngle(static_cast<uint16_t>(raw)), 0);
                REQUIRE(simScalarToFloat(w.x) == 0.0f);
                REQUIRE(simScalarToFloat(w.y) == 0.0f);
                REQUIRE(simScalarToFloat(w.z) == 0.0f);
            }
        }
    }

    TEST_CASE("the simulation keeps the wind it draws", "[wind]")
    {
        // updateWind draws speed and direction as locals and throws them away,
        // so currentWindVector is the only surviving form of the wind. This is
        // the wiring test: the formula above can be right while nothing ever
        // calls it.
        GameSimulation sim(makeFlatTerrain(16, 16), 0u, BrainCoralWindSpeed, BrainCoralWindSpeed);

        SECTION("a fresh simulation starts becalmed rather than uninitialised")
        {
            // Vector3x default-constructs its components, so this is really
            // asking whether the member carries its own zero initialiser. If
            // it ever loses it, the first shot of a game desyncs.
            REQUIRE(simScalarToFloat(sim.currentWindVector.x) == 0.0f);
            REQUIRE(simScalarToFloat(sim.currentWindVector.y) == 0.0f);
            REQUIRE(simScalarToFloat(sim.currentWindVector.z) == 0.0f);
        }

        SECTION("the first update gives it the map's wind at the map's strength")
        {
            // nextWindSpeedChange and gameTime both start at zero, so the very
            // first update is a change. Direction is random, so the length is
            // what can be asserted -- and with min == max the speed is not.
            sim.updateWind();

            auto x = simScalarToFloat(sim.currentWindVector.x);
            auto z = simScalarToFloat(sim.currentWindVector.z);
            REQUIRE(std::sqrt((x * x) + (z * z)) == Approx(BrainCoralDrift));
            REQUIRE(simScalarToFloat(sim.currentWindVector.y) == 0.0f);
        }
    }

    TEST_CASE("the wind reaches the rounds that are supposed to feel it", "[wind]")
    {
        // The three cases above can all pass while nothing in
        // updateProjectiles ever reads the vector. This is the test for the
        // two lines that do, one in the ballistic branch and one in the bomb
        // branch, and it is written so that deleting either fails it.
        //
        // The wind is set by hand rather than drawn, because what is under
        // test here is whether the vector is applied -- not whether it is
        // computed, which is already pinned above. A fixed wind also makes the
        // arithmetic exact instead of approximate. It is larger than any real
        // map's wind so the figures stay legible.
        constexpr int Ticks = 40;
        constexpr float MuzzleVelocity = 10.0f;
        constexpr float StartHeight = 1000.0f;
        constexpr float WindX = 0.25f;
        constexpr float WindZ = -0.5f;

        auto flyDownrange = [&](const ProjectilePhysicsType& physics) {
            GameSimulation sim(makeWideTerrain(), 0u, BrainCoralWindSpeed, BrainCoralWindSpeed);
            auto us = addPlayer(sim, "us");
            defineTestGun(sim, "gun", physics);

            // One tick to let updateWind fire, since it redraws on the first
            // tick and then holds for five to fourteen seconds. Overwriting
            // afterwards is safe for the whole flight: the next change is at
            // least 150 ticks away and this run is 40.
            sim.tick();
            sim.currentWindVector = SimVector(SimScalar(WindX), 0_ss, SimScalar(WindZ));

            UnitWeapon weapon;
            weapon.weaponType = "gun";
            sim.spawnProjectile(ProjectileSpawn{
                .owner = us,
                .weapon = &weapon,
                .position = SimVector(0_ss, SimScalar(StartHeight), 0_ss),
                .direction = SimVector(1_ss, 0_ss, 0_ss),
                .distanceToTarget = 500_ss,
                .targetUnit = std::nullopt,
                .attacker = std::nullopt});

            tick(sim, Ticks);

            // Fired level from a thousand up over flat ground, so it is still
            // in the air and still inside the map. If this trips, the round
            // was culled and nothing below means anything.
            REQUIRE(sim.projectiles.begin() != sim.projectiles.end());
            return sim.projectiles.begin()->second.position;
        };

        // Each tick adds the wind to the position and then the velocity, and
        // nothing slows a shell down, so both horizontal components advance by
        // a constant amount every tick.
        constexpr float ExpectedX = Ticks * (MuzzleVelocity + WindX);
        constexpr float ExpectedZ = Ticks * WindZ;

        // Gravity is taken off the velocity before the position moves, so the
        // drop after N ticks is g * N * (N + 1) / 2 rather than g * N * N / 2.
        constexpr float ExpectedY = StartHeight - (GravityPerTickSquared * Ticks * (Ticks + 1) / 2.0f);

        SECTION("a ballistic shell")
        {
            auto p = flyDownrange(ProjectilePhysicsTypeBallistic());

            // 410 rather than 400: the wind is worth a quarter of a unit a
            // tick downrange on top of the muzzle velocity.
            REQUIRE(simScalarToFloat(p.x) == Approx(ExpectedX));

            // Fired straight down +X, so every inch of this is the wind.
            REQUIRE(simScalarToFloat(p.z) == Approx(ExpectedZ));

            // And the wind has not touched the arc: this is gravity alone.
            REQUIRE(simScalarToFloat(p.y) == Approx(ExpectedY).margin(0.01f));
        }

        SECTION("a dropped bomb, which takes it from the same branch")
        {
            auto p = flyDownrange(ProjectilePhysicsTypeBomb());
            REQUIRE(simScalarToFloat(p.x) == Approx(ExpectedX));
            REQUIRE(simScalarToFloat(p.z) == Approx(ExpectedZ));
            REQUIRE(simScalarToFloat(p.y) == Approx(ExpectedY).margin(0.01f));
        }

        SECTION("a line-of-sight round, which does not")
        {
            // The contrast that makes the two above mean something: the same
            // shot from a weapon whose branch takes no wind holds its line
            // exactly, and keeps its height too, since gravity is in that
            // branch as well.
            auto p = flyDownrange(ProjectilePhysicsTypeLineOfSight());
            REQUIRE(simScalarToFloat(p.x) == Approx(Ticks * MuzzleVelocity));
            REQUIRE(simScalarToFloat(p.z) == Approx(0.0f).margin(1e-6f));
            REQUIRE(simScalarToFloat(p.y) == Approx(StartHeight));
        }
    }
}
