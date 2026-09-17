#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

/**
 * WHICH TICK A NEW ROUND TAKES ITS FIRST STEP, driven end to end through
 * GameSimulation::tick.
 *
 * This is NOT a corpus test and carries no mined numbers. weaponflight.test.cpp
 * spawns its projectile outside tick() and counts the ticks to the victim, so it
 * pins how far a step goes and where the round stops but says nothing about
 * where the firing itself sits relative to the first step. That is the gap this
 * closes, because the demo corpus cannot: a demo stamps a shot and its damage
 * from opposite sides of the same tick (below), so the interval it records is a
 * step count and not an answer to this question.
 *
 * WHAT THE ORIGINAL DOES. One tick of TotalA.exe is, in order (0x4954BD):
 * increment the game tick; the unit pass 0x48AD30; the projectile pass
 * 0x49B720; the feature pass; the per-player settle. A firing weapon reaches
 * 0x49D77E inside the unit pass and creates the round there and then -- it goes
 * on the end of the flat projectile array at globals+0x141F3/0x141F7, and its
 * burst counter proj+0x60 comes from the weapon's own `burst` (wdef+0xEA,
 * default 0 at 0x42E619), so an ordinary weapon's round is a flying round and
 * not a burst template. The projectile pass then reads that array's length ONCE
 * into its trip counter (0x49B728, stored at 0x49B740) and walks it, so the
 * round created a moment earlier in the same tick IS walked: it moves
 * (0x49BD41), and 0x49B090 tests the square it now stands in straight
 * afterwards (0x49BD88). A hit runs 0x499EB0 -> 0x499CD0 -> 0x489BB0
 * synchronously, which is what queues the damage record.
 *
 * So a round fired on tick T takes its first step ON TICK T, and its damage
 * lands on tick T + k - 1 where k is the number of steps to the victim's
 * footprint. (Only a burst CONTINUATION is a tick later, because 0x49B810
 * appends it past the trip count the pass had already latched.)
 *
 * WHY THE CORPUS READS T + k INSTEAD. A demo stamps a subpacket with the last
 * 0x2c before it in its sender's stream, and the 0x2c is built at the end of
 * that player's unit sub-pass (0x48B003). The shot record is queued before it
 * and the damage record after it, so a shot reads one tick early and its damage
 * reads true, and the difference of the two is k. docs/TA-DEMOS.md, "Which tick
 * a round first moves on", has the stream measurement that confirms it.
 *
 * WHAT RWE DOES. GameSimulation::tick increments gameTime, runs the behaviour
 * pass -- where tryFireWeapon calls spawnProjectile, which emplaces into
 * `projectiles` -- and then updateProjectiles, which moves every projectile
 * including that one and tests the collision immediately after the move,
 * applying the damage inline. Same order, same answer. These cases pin it, so a
 * rearrangement of tick() that deferred a new round by a tick would fail here
 * rather than silently putting every weapon in the game one tick behind the
 * original.
 *
 * THE NUMBERS ARE REAL. ARMHLT.fbi, CORMEX.fbi and LASER_HEAVY out of
 * weaponE/TAESC.tdf, the same data set the corpus fixture is mined from
 * (~/ta-mods/x-esc). Two deliberate departures, neither of which can move a
 * tick: the mount is stood up as an immobile mobile unit rather than a building
 * with a yard map, and the victim is given hit points it does not have (its real
 * MaxDamage is 175, against this laser's 300) so that it survives the shot and
 * the tick its health drops can be read off it.
 */
namespace rwe
{
    namespace
    {
        /**
         * 1024 squares of 16 world units, flat at zero, which puts the world
         * origin on a square boundary -- so the arithmetic in each case below
         * can be done in whole squares.
         */
        constexpr int MapSquares = 1024;

        MapTerrain makeFlatTerrain()
        {
            Grid<unsigned char> heights(MapSquares, MapSquares, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * The barrel, twenty units up. Both units are stood at this height so
         * the round flies level: a round aimed downhill would reach the flat
         * ground before the footprint and the terrain test comes first, which
         * would be measuring something else entirely.
         */
        constexpr int BarrelHeight = 20;

        void registerModels(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{
                UnitPieceDefinition{"base", SimVector(0_ss, SimScalar(BarrelHeight), 0_ss), std::nullopt}};
            sim.unitModelDefinitions["ARMHLT"] = createUnitModelDefinition(40_ss, std::move(pieces));

            std::vector<UnitPieceDefinition> victimPieces{
                UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["CORMEX"] = createUnitModelDefinition(200_ss, std::move(victimPieces));
        }

        /**
         * LASER_HEAVY out of weaponE/TAESC.tdf: lineofsight, turret, range 600,
         * reloadtime 0.2, weaponvelocity 960, areaofeffect 16, tolerance 1000,
         * energypershot 150, [DAMAGE] default 300. It names no `accuracy` and no
         * `sprayangle`, so an undamaged mount fires exactly where it is pointed
         * -- which is why this weapon and not another: a round that strayed
         * could enter the footprint on a different step.
         */
        void defineLaser(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 600_ss;
            w.reloadTime = SimScalar(0.2f);
            w.burst = 1;
            w.velocity = 960_ss / 30_ss;
            w.damageRadius = 16_ss;
            w.damage["DEFAULT"] = 300;
            w.turret = true;
            w.tolerance = SimAngle(1000);
            w.pitchTolerance = SimAngle(1000);
            w.energyPerShot = Energy(150.0f);
            sim.weaponDefinitions["LASER_HEAVY"] = w;
        }

        /** ARMHLT.fbi: FootprintX/Z 2, MaxDamage 3075, SightDistance 512, Weapon1=LASER_HEAVY. */
        void defineShooter(GameSimulation& sim)
        {
            UnitDefinition d{};
            d.objectName = "ARMHLT";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = true;
            d.sightDistance = 512u;
            d.maxHitPoints = 3075;
            d.buildTime = 0u;
            d.shootMe = true;
            d.standingFireOrder = UnitFireOrders::FireAtWill;
            d.category = "ARM BLDG NAIR NSUB NSHP NLND NEXP NNUK ALL LEV2";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            d.weapon1 = "LASER_HEAVY";
            sim.unitDefinitions["ARMHLT"] = d;
        }

        /**
         * CORMEX.fbi: FootprintX/Z 3, SightDistance 192. Its real MaxDamage of
         * 175 is raised so one shot does not kill it -- see the header.
         */
        void defineVictim(GameSimulation& sim)
        {
            UnitDefinition d{};
            d.objectName = "CORMEX";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = false;
            d.sightDistance = 192u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "CORE BLDG NAIR NSUB NSHP NLND NEXP NNUK ALL LEV1";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["CORMEX"] = d;
        }

        struct Result
        {
            /** The tick the FireWeaponEvent came out on, counting from one. */
            unsigned int fireTick;

            /** The tick the victim's health dropped. */
            unsigned int damageTick;
        };

        /**
         * Stand a Sentinel at the origin and a metal extractor `distance` world
         * units up the z axis from it, let the tower find the target on its own
         * and shoot it, and report which tick each half happened on.
         */
        Result fireAt(int distance)
        {
            GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);

            // Circular sight, which is one of the original's own two modes and
            // the one whose reach does not depend on line-of-sight tables a
            // headless test has not loaded. Without it a tower can only see
            // about a hundred and sixty units and the longest case below never
            // acquires a target at all. Nothing about sight can move a tick
            // once the shot has gone.
            sim.lineOfSightMode = LineOfSightMode::Circular;

            auto us = addPlayer(sim, "us");
            auto them = addPlayer(sim, "them");
            registerModels(sim);
            defineLaser(sim);
            defineShooter(sim);
            defineVictim(sim);

            auto script = makeEmptyCobScript({"base"});
            sim.unitScriptDefinitions["ARMHLT"] = *script;
            sim.unitScriptDefinitions["CORMEX"] = *script;

            auto shooterId = sim.trySpawnUnit("ARMHLT", us, SimVector(0_ss, 0_ss, 0_ss), std::nullopt);
            REQUIRE(shooterId);
            auto victimId = sim.trySpawnUnit("CORMEX", them, SimVector(0_ss, 0_ss, SimScalar(distance)), std::nullopt);
            REQUIRE(victimId);

            {
                auto& shooter = sim.getUnitState(*shooterId);
                shooter.hitPoints = sim.unitDefinitions.at("ARMHLT").maxHitPoints;
                auto& victim = sim.getUnitState(*victimId);
                victim.hitPoints = sim.unitDefinitions.at("CORMEX").maxHitPoints;
                // Level flight; see BarrelHeight.
                victim.position.y = SimScalar(BarrelHeight);
                victim.previousPosition = victim.position;
            }

            auto fullHealth = sim.getUnitState(*victimId).hitPoints;
            std::optional<unsigned int> fireTick;

            for (unsigned int t = 1; t <= 600u; ++t)
            {
                sim.events.clear();
                sim.tick();

                if (!fireTick)
                {
                    for (const auto& e : sim.events)
                    {
                        if (std::get_if<FireWeaponEvent>(&e) != nullptr)
                        {
                            fireTick = t;
                            break;
                        }
                    }
                }

                if (sim.getUnitState(*victimId).hitPoints < fullHealth)
                {
                    REQUIRE(fireTick.has_value());
                    return Result{*fireTick, t};
                }
            }

            FAIL("the shot never arrived");
            return Result{0u, 0u};
        }
    }

    TEST_CASE("a round fired through tick() takes its first step on the tick it is fired", "[weapon]")
    {
        // The victim is 3x3 squares, so its footprint reaches 24 world units
        // towards the shooter from the aim point, and the round steps 32 units a
        // tick from the barrel at z = 0. The distances below are chosen so the
        // near edge of the footprint lands exactly on a square boundary and the
        // step count is not a matter of rounding.

        SECTION("one step: the damage lands on the tick of the shot itself")
        {
            // Footprint from z = 32 to z = 80. The first step puts the round at
            // z = 32, inside it -- so if the round moves on the tick it is
            // fired, the shot and the damage share a tick. This is the case the
            // whole question comes down to.
            auto r = fireAt(56);
            REQUIRE(r.damageTick == r.fireTick);
        }

        SECTION("four steps: three ticks after the shot")
        {
            // Footprint from z = 128 to z = 176. Steps land at 32, 64, 96, 128:
            // the fourth is the first one inside.
            auto r = fireAt(152);
            REQUIRE(r.damageTick == r.fireTick + 3u);
        }

        SECTION("ten steps: nine ticks after the shot")
        {
            // Footprint from z = 320 to z = 368, and the tenth step is the first
            // one in it. Far enough out that an off-by-one in the step count
            // could not be mistaken for an off-by-one in the firing tick.
            auto r = fireAt(344);
            REQUIRE(r.damageTick == r.fireTick + 9u);
        }
    }
}
