#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/LosTables.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeGunshipTerrain(int width, int height)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addGunshipPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeGunshipScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerGunshipModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** The ARM Brawler, straight out of ARMBRAWL.FBI. */
        UnitDefinition makeGunshipDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.canFly = true;
            d.hoverAttack = true;
            d.cruiseAltitude = 60_ss;
            d.maxVelocity = 6.6_ssf;
            d.acceleration = 0.16_ssf;
            d.brakeRate = 4_ss;
            d.turnRate = 800_ss;
            d.maneuverLeashLength = 1280_ss;
            d.sightDistance = 350u;
            d.shootMe = true;
            d.maxHitPoints = 920;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitDefinition makeGunshipTargetDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.shootMe = true;
            d.sightDistance = 100u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** VTOL_EMG, the Brawler's gun: range 370, four rounds every six tenths. */
        void defineGun(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.maxRange = 370_ss;
            w.reloadTime = SimScalar(0.6f);
            w.burst = 4;
            w.burstInterval = SimScalar(0.1f);
            w.velocity = 450_ss / 30_ss;
            w.damageRadius = 4_ss;
            w.damage["DEFAULT"] = 10;
            sim.weaponDefinitions["emg"] = w;
        }

        UnitId spawnGunshipUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        SimScalar gunshipFlatDistance(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
        }
    }

    TEST_CASE("a gunship that picks its own target still works the ring", "[gunship]")
    {
        // Reported from play: Brawlers and Rapiers were not swinging. The ring
        // behaviour was only ever reached from an explicit attack order or
        // from a patrol engaging en route, so a gunship left to fire at will
        // acquired a target through the ordinary weapon path and then shot at
        // it from wherever it happened to be -- and an aircraft with no orders
        // goes looking for somewhere to land, so it would break off and set
        // down while still shooting. Nobody plays by right-clicking every
        // target, so this is the case that matters.
        auto script = makeGunshipScript();
        GameSimulation sim(makeGunshipTerrain(256, 256), 0u, 0, 0);
        auto us = addGunshipPlayer(sim, "us");
        auto them = addGunshipPlayer(sim, "them");
        sim.unitDefinitions["gunship"] = makeGunshipDef();
        sim.unitDefinitions["target"] = makeGunshipTargetDef();
        registerGunshipModel(sim, "model");
        defineGun(sim);
        // Without ray fans a unit sees only the cell it stands in, and
        // nothing can acquire anything. Eight is the original’s own cap.
        sim.losTables = generateLosTables(8);

        auto targetPosition = SimVector(0_ss, 0_ss, 0_ss);
        auto targetId = spawnGunshipUnit(sim, "target", them, targetPosition, script);
        sim.getUnitState(targetId).hitPoints = 1000000;

        // Inside its own gun range to begin with, so it acquires at once.
        auto gunshipId = spawnGunshipUnit(sim, "gunship", us, SimVector(0_ss, 60_ss, -200_ss), script);
        {
            auto& g = sim.getUnitState(gunshipId);
            g.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "emg";
            g.weapons[0] = weapon;
            // No orders at all: this is a gunship minding its own business.
        }
        sim.flyingUnitsSet.insert(gunshipId);

        const auto ring = (2_ss * 370_ss) / 3_ss;
        auto startingHitPoints = sim.getUnitState(targetId).hitPoints;
        int measured = 0;
        int onStation = 0;
        auto closest = SimScalar(100000.0f);
        auto lowest = SimScalar(100000.0f);

        for (int tick = 0; tick < 1200; ++tick)
        {
            sim.tick();
            const auto& g = sim.getUnitState(gunshipId);
            auto d = gunshipFlatDistance(g.position, targetPosition);
            if (std::getenv("RWE_TRACE_GUNSHIP") && tick % 30 == 0)
            {
                std::cout << "t=" << tick << " d=" << simScalarToFloat(d)
                          << " y=" << simScalarToFloat(g.position.y) << std::endl;
            }
            if (tick < 300)
            {
                continue;
            }
            ++measured;
            closest = rweMin(closest, d);
            lowest = rweMin(lowest, g.position.y);
            if (d > ring * 0.8_ssf && d < ring * 1.2_ssf)
            {
                ++onStation;
            }
        }

        CAPTURE(measured);
        CAPTURE(onStation);
        CAPTURE(simScalarToFloat(closest));
        CAPTURE(simScalarToFloat(lowest));

        // It is shooting...
        REQUIRE(sim.getUnitState(targetId).hitPoints < startingHitPoints);
        // ...it stays in the air rather than breaking off to land...
        REQUIRE(lowest > 30_ss);
        // ...and it settles onto the ring instead of firing from wherever.
        REQUIRE(onStation * 100 / measured >= 80);
    }

    TEST_CASE("a gunship works its target from a standoff ring instead of running past it", "[gunship]")
    {
        // A Brawler or a Rapier does not fly attack runs. It closes to inside
        // its own weapon range and then works its way around the target,
        // keeping the gun on it throughout. What it must not do is what a
        // bomber does: blow through, run out several hundred units, turn round
        // and come back, which leaves the target unshot for most of the fight.
        auto script = makeGunshipScript();
        GameSimulation sim(makeGunshipTerrain(256, 256), 0u, 0, 0);
        auto us = addGunshipPlayer(sim, "us");
        auto them = addGunshipPlayer(sim, "them");
        sim.unitDefinitions["gunship"] = makeGunshipDef();
        sim.unitDefinitions["target"] = makeGunshipTargetDef();
        registerGunshipModel(sim, "model");
        defineGun(sim);
        // Without ray fans a unit sees only the cell it stands in, and
        // nothing can acquire anything. Eight is the original’s own cap.
        sim.losTables = generateLosTables(8);

        auto targetPosition = SimVector(0_ss, 0_ss, 0_ss);
        auto targetId = spawnGunshipUnit(sim, "target", them, targetPosition, script);
        sim.getUnitState(targetId).hitPoints = 1000000;

        auto gunshipId = spawnGunshipUnit(sim, "gunship", us, SimVector(0_ss, 60_ss, -900_ss), script);
        {
            auto& g = sim.getUnitState(gunshipId);
            g.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "emg";
            g.weapons[0] = weapon;
            g.orders.push_back(createAttackOrder(targetId));
        }
        sim.flyingUnitsSet.insert(gunshipId);

        const auto weaponRange = 370_ss;
        // The ring the original works from: two thirds of the weapon's reach,
        // so 246 units for a Brawler. Flying the chord between two stations
        // 45 degrees apart takes it in as close as r*cos(22.5) = 228 at the
        // midpoint, and never further out than the ring itself.
        const auto ring = (2_ss * weaponRange) / 3_ss;

        int ticksInRange = 0;
        int measured = 0;
        auto closest = SimScalar(100000.0f);
        auto furthest = 0_ss;
        auto startingHitPoints = sim.getUnitState(targetId).hitPoints;

        for (int tick = 0; tick < 1800; ++tick)
        {
            sim.tick();
            const auto& g = sim.getUnitState(gunshipId);
            auto d = gunshipFlatDistance(g.position, targetPosition);
            if (std::getenv("RWE_TRACE_GUNSHIP") && tick % 30 == 0)
            {
                std::cout << "t=" << tick << " d=" << simScalarToFloat(d)
                          << " pos=" << simScalarToFloat(g.position.x) << "," << simScalarToFloat(g.position.z) << std::endl;
            }

            // Give it four hundred ticks to fly in from 900 units out, then
            // measure only the settled behaviour.
            if (tick < 400)
            {
                continue;
            }
            ++measured;
            closest = rweMin(closest, d);
            furthest = rweMax(furthest, d);
            if (d <= weaponRange)
            {
                ++ticksInRange;
            }
        }

        CAPTURE(simScalarToFloat(ring));
        CAPTURE(simScalarToFloat(closest));
        CAPTURE(simScalarToFloat(furthest));

        REQUIRE(measured > 0);
        // It never stops working the target...
        REQUIRE(ticksInRange == measured);
        // ...it holds the ring rather than closing on the target or wandering
        // off it, which is what tells a gunship apart from an attack run...
        REQUIRE(closest > ring * 0.8_ssf);
        REQUIRE(furthest < ring * 1.2_ssf);
        // ...and it is shooting the whole time it does so.
        REQUIRE(sim.getUnitState(targetId).hitPoints < startingHitPoints);
    }
}
