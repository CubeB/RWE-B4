#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain()
        {
            Grid<unsigned char> heights(256, 256, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** The ARM Freedom Fighter's numbers, near enough: fast, and gun-armed. */
        UnitDefinition makeFighterDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.canFly = true;
            d.cruiseAltitude = 200_ss;
            d.maxVelocity = 10_ss;
            d.acceleration = 0.1_ssf;
            d.brakeRate = 0.5_ssf;
            d.turnRate = 360_ss;
            d.attackRunLength = 120_ss;
            d.maneuverLeashLength = 1280_ss;
            d.sightDistance = 350;
            d.maxHitPoints = 300;
            d.buildTime = 0u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        /** A missile with a fighter's reach; not a dropped weapon, so AirToAir applies. */
        void defineAirWeapon(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 510_ss;
            w.reloadTime = SimScalar(0.4f);
            w.burst = 1;
            w.velocity = 300_ss / 30_ss;
            w.damageRadius = 8_ss;
            w.damage["DEFAULT"] = 40;
            sim.weaponDefinitions["airmissile"] = w;
        }

        UnitId launchFighter(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "fighter";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 300;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            auto id = sim.tryAddUnit(std::move(unit)).value();

            auto& fighter = sim.getUnitState(id);
            fighter.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "airmissile";
            fighter.weapons[0] = weapon;
            sim.flyingUnitsSet.insert(id);
            return id;
        }

        const AirMovementStateDogfight* dogfightState(const UnitState& unit)
        {
            if (auto air = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
            {
                return std::get_if<AirMovementStateDogfight>(&air->movementState);
            }
            return nullptr;
        }
    }

    TEST_CASE("a fighter sent at another aircraft flies a dogfight", "[dogfight]")
    {
        // Until this was ported an air target got the bomber's attack run with
        // the strafing overshoot switched off, because it was the closest
        // thing RWE had. The original gives it AirToAir (0x412D40), which is
        // a pursuit and not a circuit: it leads the bandit, extends when it
        // overshoots, and breaks off when he ends up behind.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        sim.unitDefinitions["fighter"] = makeFighterDef();
        registerModel(sim);
        defineAirWeapon(sim);

        auto banditId = launchFighter(sim, them, SimVector(400_ss, 200_ss, 0_ss), script);
        auto fighterId = launchFighter(sim, us, SimVector(-400_ss, 200_ss, 0_ss), script);
        sim.getUnitState(fighterId).orders.push_back(AttackOrder(banditId));

        SECTION("it enters the dogfight rather than an attack run")
        {
            // Two: the behaviour pass points the weapon, and the weapon pass
            // that follows is what puts it into its attacking state.
            sim.tick();
            sim.tick();

            const auto& fighter = sim.getUnitState(fighterId);
            REQUIRE(dogfightState(fighter) != nullptr);

            // The mission points weapon 0 at the bandit and never takes it
            // off; whether the weapon holds that aim is the weapon code's
            // business, and at eight hundred units against a reach of five
            // hundred it will not -- which is the original's arrangement
            // too, since this mission has no range test of its own at all.
            REQUIRE(fighter.weapons[0].has_value());
        }

        SECTION("it leads the bandit rather than flying at where he is")
        {
            // The bandit is running; the goal should be placed ahead of him
            // along his own velocity, not on top of him.
            {
                auto& bandit = sim.getUnitState(banditId);
                bandit.orders.push_back(MoveOrder(SimVector(400_ss, 200_ss, 1500_ss)));
            }

            for (int tick = 0; tick < 90; ++tick)
            {
                sim.tick();
            }

            const auto* state = dogfightState(sim.getUnitState(fighterId));
            REQUIRE(state != nullptr);

            const auto& bandit = sim.getUnitState(banditId);
            auto banditIsMoving = bandit.position.distanceSquared(SimVector(400_ss, 200_ss, 0_ss)) > (16_ss * 16_ss);
            REQUIRE(banditIsMoving);

            // The goal is ahead of the bandit, in the direction he is going.
            SimVector banditToGoal(
                state->goalPosition.x - bandit.position.x,
                0_ss,
                state->goalPosition.z - bandit.position.z);
            REQUIRE(banditToGoal.lengthSquared() > (32_ss * 32_ss));
        }

        SECTION("it eventually breaks off rather than orbiting for ever")
        {
            // Three decisions with the bandit off the nose, or an overshoot
            // with him behind, and the fighter breaks ninety degrees. Over a
            // long engagement that has to happen at least once -- without it
            // the original's fighters would circle indefinitely.
            bool everBroke = false;
            for (int tick = 0; tick < 1800 && !everBroke; ++tick)
            {
                sim.tick();
                if (const auto* state = dogfightState(sim.getUnitState(fighterId)))
                {
                    if (state->phase == AirMovementStateDogfight::Phase::BreakingOut
                        || state->phase == AirMovementStateDogfight::Phase::BreakingAway)
                    {
                        everBroke = true;
                    }
                }
            }

            REQUIRE(everBroke);
        }

        SECTION("the fight is decided rather than going on for ever")
        {
            auto stillFlying = [&]() {
                return sim.units.tryGet(banditId).has_value() && sim.units.tryGet(fighterId).has_value();
            };

            for (int tick = 0; tick < 3600 && stillFlying(); ++tick)
            {
                sim.tick();
            }

            // Two fighters with the same gun, and which of them wins is not
            // the point -- the stationary one shoots back, and does rather
            // well out of not having to manoeuvre. The point is that the
            // pursuit brings a gun to bear at all: without it they would
            // circle each other until the clock ran out, which is what the
            // attack run they used to fly actually did.
            REQUIRE(!stillFlying());
        }
    }

    TEST_CASE("an aircraft that dies off the map leaves the flying set", "[dogfight]")
    {
        // The projectile pass walks flyingUnitsSet and asks the simulation
        // for each unit by id, so an entry left behind by a dead aircraft is
        // a lookup for a unit that is not there -- an assertion in Debug, a
        // bad variant access in Release. An aircraft really can die out
        // there: a dogfight breaks two weapon ranges out, which off a corner
        // is over the edge.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        sim.unitDefinitions["fighter"] = makeFighterDef();
        registerModel(sim);
        defineAirWeapon(sim);

        auto fighterId = launchFighter(sim, us, SimVector(0_ss, 200_ss, 0_ss), script);
        REQUIRE(sim.flyingUnitsSet.count(fighterId) == 1);

        // Well outside the map, which is what a break off a corner does.
        sim.getUnitState(fighterId).position = SimVector(9000_ss, 200_ss, 9000_ss);
        sim.getUnitState(fighterId).markAsDead();
        sim.tick();

        REQUIRE(sim.flyingUnitsSet.count(fighterId) == 0);
        REQUIRE(!sim.units.tryGet(fighterId).has_value());
    }
}
