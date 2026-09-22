#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/LineOfFire.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

/**
 * Shooting into a hillside.
 *
 * Reported from a replay on Crystal Maze: units "spend a lot of time
 * attempting to shoot at enemy units through elevation and their projectiles
 * never reach". The simulation is behaving correctly -- it has no
 * line-of-fire test and neither has the original -- so what is wrong is that
 * the computer player never asks the question a human answers by looking at
 * the screen. These are that question and what it does about it.
 */
namespace rwe
{
    namespace
    {
        /**
         * Flat, with a wall of the given height standing across the middle.
         *
         * A 64-tile map is centred on the origin, so the middle columns are
         * world x around zero and the units below stand either side of them.
         */
        MapTerrain makeRidgeTerrain(unsigned char ridgeHeight)
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (int z = 0; z < heights.getHeight(); ++z)
            {
                for (int x = 30; x <= 34; ++x)
                {
                    heights.set(x, z, ridgeHeight);
                }
            }
            return MapTerrain(std::move(heights), 0_ss);
        }

        UnitDefinition makeGunDef(const std::string& weapon)
        {
            UnitDefinition d{};
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = !weapon.empty();
            d.weapon1 = weapon;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = 1000u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        void defineWeapons(GameSimulation& sim)
        {
            WeaponDefinition laser{};
            laser.maxRange = 600_ss;
            laser.physicsType = ProjectilePhysicsTypeLineOfSight{};
            sim.weaponDefinitions["LASER"] = laser;

            // A shell that lobs. Same reach, and the ridge is none of its
            // business.
            WeaponDefinition mortar{};
            mortar.maxRange = 600_ss;
            mortar.physicsType = ProjectilePhysicsTypeBallistic{};
            sim.weaponDefinitions["MORTAR"] = mortar;

            sim.unitDefinitions["GUN"] = makeGunDef("LASER");
            sim.unitDefinitions["MORTARKBOT"] = makeGunDef("MORTAR");
            auto flier = makeGunDef("LASER");
            flier.canFly = true;
            sim.unitDefinitions["FLIER"] = flier;
        }
    }

    TEST_CASE("a ridge between two units stops the shot", "[lineoffire]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeRidgeTerrain(100u), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineWeapons(sim);

        // Either side of the wall, which stands across the middle of the map.
        SimVector here(-300_ss, 0_ss, 0_ss);
        SimVector there(300_ss, 0_ss, 0_ss);

        // The wall really is between them and really is high.
        REQUIRE(sim.terrain.getHeightAt(0_ss, 0_ss) > 50_ss);
        REQUIRE(sim.terrain.getHeightAt(here.x, here.z) < 10_ss);
        REQUIRE(sim.terrain.getHeightAt(there.x, there.z) < 10_ss);
        auto ours = addUnitOfType(sim, "GUN", us, here, script);
        auto theirs = addUnitOfType(sim, "GUN", them, there, script);

        const auto& shooter = sim.getUnitState(ours);
        const auto& target = sim.getUnitState(theirs);

        REQUIRE(terrainBlocksShot(sim, shooter, target));
        REQUIRE_FALSE(shotClearsTerrain(sim, here, there));

        SECTION("and standing still is not the answer to it")
        {
            auto stand = positionForClearShot(sim, shooter, target);
            REQUIRE(stand.has_value());
            // Forward, towards the target, not back and not where it
            // already stands.
            REQUIRE(stand->x > here.x);
            REQUIRE(stand->x <= there.x);
        }
    }

    TEST_CASE("flat ground between two units does not", "[lineoffire]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeRidgeTerrain(0u), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineWeapons(sim);

        auto ours = addUnitOfType(sim, "GUN", us, SimVector(-300_ss, 0_ss, 0_ss), script);
        auto theirs = addUnitOfType(sim, "GUN", them, SimVector(300_ss, 0_ss, 0_ss), script);

        REQUIRE_FALSE(terrainBlocksShot(sim, sim.getUnitState(ours), sim.getUnitState(theirs)));
        // Nothing to answer, so nowhere to go.
        REQUIRE_FALSE(positionForClearShot(sim, sim.getUnitState(ours), sim.getUnitState(theirs)).has_value());
    }

    TEST_CASE("a weapon that lobs is not asked about the ridge", "[lineoffire]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeRidgeTerrain(100u), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineWeapons(sim);

        auto ours = addUnitOfType(sim, "MORTARKBOT", us, SimVector(-300_ss, 0_ss, 0_ss), script);
        auto theirs = addUnitOfType(sim, "GUN", them, SimVector(300_ss, 0_ss, 0_ss), script);

        // The ground is in the way of a straight line and the shell does not
        // travel in one. Asking would give the wrong answer, so it is not
        // asked.
        REQUIRE_FALSE(shotClearsTerrain(sim, SimVector(-300_ss, 0_ss, 0_ss), SimVector(300_ss, 0_ss, 0_ss)));
        REQUIRE_FALSE(terrainBlocksShot(sim, sim.getUnitState(ours), sim.getUnitState(theirs)));
    }

    TEST_CASE("nothing stands between an aircraft and the ground", "[lineoffire]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeRidgeTerrain(100u), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineWeapons(sim);

        auto ours = addUnitOfType(sim, "FLIER", us, SimVector(-300_ss, 200_ss, 0_ss), script);
        auto theirs = addUnitOfType(sim, "GUN", them, SimVector(300_ss, 0_ss, 0_ss), script);
        auto shooter = addUnitOfType(sim, "GUN", us, SimVector(-300_ss, 0_ss, 0_ss), script);
        auto flying = addUnitOfType(sim, "FLIER", them, SimVector(300_ss, 200_ss, 0_ss), script);

        // Both ways round: a gunship over the wall, and a kbot shooting at
        // something above it. This is why the air arm is the answer to a map
        // that keeps the ground arm apart -- see AirManager's gunships.
        REQUIRE_FALSE(terrainBlocksShot(sim, sim.getUnitState(ours), sim.getUnitState(theirs)));
        REQUIRE_FALSE(terrainBlocksShot(sim, sim.getUnitState(shooter), sim.getUnitState(flying)));
    }
}
