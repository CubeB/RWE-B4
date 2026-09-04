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
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width, int height)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** The ARM Thunder, straight out of ARMTHUND.FBI, sight distance included. */
        UnitDefinition makeBomberDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.canFly = true;
            d.canPatrol = true;
            d.cruiseAltitude = 200_ss;
            d.maxVelocity = 9_ss;
            d.acceleration = 0.08_ssf;
            d.brakeRate = 0.4_ssf;
            d.turnRate = 356_ss;
            d.attackRunLength = 120_ss;
            d.maneuverLeashLength = 1280_ss;
            d.sightDistance = 350;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitDefinition makeTargetDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 10000;
            d.buildTime = 0u;
            // Every combat unit in the shipped data carries ShootMe=1; without
            // it a passing unit leaves this alone, which is the rule that
            // keeps armies from stopping to shoot at metal extractors.
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** The ARM bomb: dropped, range 1280, 48 across, no launch speed of its own. */
        void defineBomb(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeBomb();
            w.maxRange = 1280_ss;
            w.reloadTime = SimScalar(0.18f);
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = 0_ss;
            w.damageRadius = 24_ss;
            w.damage["DEFAULT"] = 100;
            sim.weaponDefinitions["bomb"] = w;
        }

        UnitId spawnUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        UnitId launchBomber(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto bomberId = spawnUnit(sim, "bomber", owner, pos, script);
            auto& bomber = sim.getUnitState(bomberId);
            bomber.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "bomb";
            bomber.weapons[0] = weapon;
            sim.flyingUnitsSet.insert(bomberId);
            return bomberId;
        }
    }

    TEST_CASE("a bomber on patrol bombs what its route takes it past", "[patrol][bomber]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        sim.unitDefinitions["bomber"] = makeBomberDef();
        sim.unitDefinitions["target"] = makeTargetDef();
        registerModel(sim);
        defineBomb(sim);

        // A long east-west leg across the middle of the map, flown back and
        // forth, with a target sitting still beside it.
        auto west = SimVector(-800_ss, 0_ss, 0_ss);
        auto east = SimVector(800_ss, 0_ss, 0_ss);

        // How far off the route the target sits, and whether a Thunder ever
        // notices it. The radius is min(SightDistance, weapon-0 range)
        // measured from the aircraft -- 350 for this one, since the bomb
        // reaches 1280 -- so the route has to carry it within 350.
        auto offset = 0_ss;
        auto expectBombed = true;
        SECTION("directly on the route") { offset = 0_ss; }
        SECTION("a little to one side") { offset = 200_ss; }
        SECTION("too far off the route to be noticed")
        {
            offset = 600_ss;
            expectBombed = false;
        }

        auto targetPosition = SimVector(0_ss, 0_ss, offset);
        auto targetId = spawnUnit(sim, "target", them, targetPosition, script);
        auto startingHitPoints = sim.getUnitState(targetId).hitPoints;

        auto bomberId = launchBomber(sim, us, SimVector(west.x, 200_ss, west.z), script);
        {
            auto& bomber = sim.getUnitState(bomberId);
            bomber.orders.push_back(PatrolOrder(east));
            bomber.orders.push_back(PatrolOrder(west));
        }

        // Long enough for several laps of the route.
        for (int tick = 0; tick < 2000; ++tick)
        {
            sim.tick();
            if (std::getenv("RWE_TRACE_PATROL") && tick % 25 == 0)
            {
                const auto& b = sim.getUnitState(bomberId);
                std::string state = "ground";
                if (auto air = std::get_if<UnitPhysicsInfoAir>(&b.physics))
                {
                    if (std::holds_alternative<AirMovementStateFlying>(air->movementState)) state = "flying";
                    else if (auto r = std::get_if<AirMovementStateAttackRun>(&air->movementState)) state = "run" + std::to_string(static_cast<int>(r->phase));
                    else state = "other";
                }
                int projectileCount = 0;
                for ([[maybe_unused]] const auto& pr : sim.projectiles) { ++projectileCount; }
                bool aiming = b.weapons[0] && !std::holds_alternative<UnitWeaponStateIdle>(b.weapons[0]->state);
                std::cout << "t=" << tick
                          << " pos=" << simScalarToFloat(b.position.x) << "," << simScalarToFloat(b.position.z)
                          << " orders=" << b.orders.size()
                          << " front=" << (b.orders.empty() ? "none" : (std::holds_alternative<PatrolOrder>(b.orders.front()) ? "patrol" : "other"))
                          << " air=" << state
                          << " aiming=" << aiming
                          << " projectiles=" << projectileCount << " detect=" << sim.canDetectUnit(us, targetId)
                          << std::endl;
            }
            if (sim.getUnitState(targetId).hitPoints < startingHitPoints)
            {
                break;
            }
        }

        auto bombed = sim.getUnitState(targetId).hitPoints < startingHitPoints;
        REQUIRE(bombed == expectBombed);
    }
}
