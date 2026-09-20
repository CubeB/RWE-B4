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
}
