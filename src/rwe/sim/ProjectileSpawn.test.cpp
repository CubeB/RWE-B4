#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/Projectile.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

/**
 * THE DESCRIPTOR DRIVES THE SPAWN.
 *
 * The interface note, not a behaviour one: both doors -- the weapon resolved
 * off a unit, and the bare type string -- must place the same round the
 * positional interface placed, and `spawnProjectile` must still emplace what
 * `createProjectileFromWeapon` returns so the pass that walks `projectiles`
 * walks it the same tick. Its tick behaviour is pinned for real in
 * weaponfiretick.test.cpp; here it is only the spawn.
 */
namespace rwe
{
    namespace
    {
        /** A straight-flying round, driven level, with no behaviour of its own. */
        void defineRound(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 240_ss;
            w.burst = 1;
            w.velocity = 10_ss / 30_ss;
            w.damage["DEFAULT"] = 42;
            sim.weaponDefinitions["round"] = w;
        }
    }

    TEST_CASE("spawnProjectile places the round on the tick it is asked for, through either door", "[projectile][spawn]")
    {
        MapTerrain terrain(Grid<unsigned char>(64, 64, static_cast<unsigned char>(0)), 0_ss);
        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim);
        defineRound(sim);

        SECTION("the resolved-weapon door")
        {
            UnitWeapon weapon;
            weapon.weaponType = "round";
            sim.spawnProjectile(ProjectileSpawn{
                .owner = PlayerId(1),
                .weapon = &weapon,
                .position = SimVector(0_ss, 20_ss, 0_ss),
                .direction = SimVector(1_ss, 0_ss, 0_ss),
                .distanceToTarget = 100_ss});

            tick(sim, 1);
            REQUIRE(anyProjectiles(sim));
            const auto& p = sim.projectiles.begin()->second;
            REQUIRE(p.weaponType == "round");
            REQUIRE(p.owner == PlayerId(1));
            REQUIRE(p.damage.at("DEFAULT") == 42u);
        }

        SECTION("the bare-type door")
        {
            sim.spawnProjectile(ProjectileSpawn{
                .owner = PlayerId(1),
                .weaponType = "round",
                .position = SimVector(0_ss, 20_ss, 0_ss),
                .direction = SimVector(1_ss, 0_ss, 0_ss),
                .distanceToTarget = 100_ss});

            tick(sim, 1);
            REQUIRE(anyProjectiles(sim));
            const auto& p = sim.projectiles.begin()->second;
            REQUIRE(p.weaponType == "round");
            REQUIRE(p.owner == PlayerId(1));
            REQUIRE(p.damage.at("DEFAULT") == 42u);
        }
    }

    TEST_CASE("a burst child lives to a little past the aim point, not to full range", "[projectile][spawn][burst]")
    {
        // BT-113. The first round of a burst is the template and lives out the
        // range over the speed cap. A child is made later by the projectile
        // pass and expires at the tick plus the weapon's timer when it has
        // one, else at (the stored horizontal distance + 16) / its own speed
        // (0x49B87F-0x49B8BC).
        MapTerrain terrain(Grid<unsigned char>(64, 64, static_cast<unsigned char>(0)), 0_ss);
        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim);

        // The shipped EMG shape: lineofsight at 300 a second (10 a tick),
        // burst 3, no weapontimer here so the distance rule is what is read.
        WeaponDefinition w{};
        w.physicsType = ProjectilePhysicsTypeLineOfSight();
        w.maxRange = 600_ss;
        w.burst = 3;
        w.velocity = 300_ss / 30_ss;
        w.damage["DEFAULT"] = 5;
        sim.weaponDefinitions["emg"] = w;

        auto spawn = [&](std::optional<SimScalar> childDistance) {
            return sim.createProjectileFromWeapon(ProjectileSpawn{
                .owner = PlayerId(1),
                .weaponType = "emg",
                .position = SimVector(0_ss, 20_ss, 0_ss),
                .direction = SimVector(1_ss, 0_ss, 0_ss),
                .distanceToTarget = 200_ss,
                .burstChildHorizontalDistance = childDistance});
        };

        SECTION("the first round flies the whole range")
        {
            auto first = spawn(std::nullopt);
            REQUIRE(first.dieOnFrame.has_value());
            REQUIRE(*first.dieOnFrame == sim.gameTime + GameTime(60));
        }

        SECTION("a child lives for (distance + 16) over its own speed")
        {
            auto child = spawn(200_ss);
            REQUIRE(child.dieOnFrame.has_value());
            // 216 / 10, truncated to whole ticks.
            REQUIRE(*child.dieOnFrame == sim.gameTime + GameTime(21));
        }

        SECTION("a weapon that names a timer gives the child that timer instead")
        {
            sim.weaponDefinitions["emg"].weaponTimer = GameTime(30);
            sim.weaponDefinitions["emg"].randomDecay = GameTime(0);
            auto child = spawn(200_ss);
            REQUIRE(child.dieOnFrame.has_value());
            REQUIRE(*child.dieOnFrame == sim.gameTime + GameTime(30));
        }
    }
}
