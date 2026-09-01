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
        MapTerrain makeIdleTerrain(int width, int height)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addIdlePlayer(GameSimulation& sim, const std::string& name)
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

        std::shared_ptr<CobScript> makeIdleScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerIdleModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** The ARM Thunder, straight out of ARMTHUND.FBI. */
        UnitDefinition makeIdleBomberDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.canFly = true;
            d.cruiseAltitude = 200_ss;
            d.maxVelocity = 9_ss;
            d.acceleration = 0.08_ssf;
            d.brakeRate = 0.4_ssf;
            d.turnRate = 356_ss;
            d.attackRunLength = 120_ss;
            d.maneuverLeashLength = 1280_ss;
            d.sightDistance = 350u;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        /** The ARM Construction Aircraft, out of ARMCA.FBI. It has no weapon at all. */
        UnitDefinition makeIdleBuilderDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canFly = true;
            d.builder = true;
            d.buildDistance = 40_ss;
            d.cruiseAltitude = 70_ss;
            d.maxVelocity = 6.9_ssf;
            d.acceleration = 0.06_ssf;
            d.brakeRate = 1.5_ssf;
            d.turnRate = 90_ss;
            d.workerTimePerTick = 2u;
            d.maxHitPoints = 280;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitDefinition makeIdleTargetDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            // Mobile but unable to move: avoids needing a yardmap.
            d.isMobile = true;
            d.canMove = false;
            d.shootMe = true;
            d.sightDistance = 100u;
            d.maxHitPoints = 10000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** The ARM bomb: dropped, range 1280. The circuit's radius comes off this. */
        void defineIdleBomb(GameSimulation& sim)
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

        UnitId spawnIdleUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        SimScalar idleFlatDistance(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
        }

        bool isLanding(const UnitState& u)
        {
            auto air = std::get_if<UnitPhysicsInfoAir>(&u.physics);
            if (air == nullptr)
            {
                return false;
            }
            return std::holds_alternative<AirMovementStateLanding>(air->movementState);
        }
    }

    TEST_CASE("a bomber whose target dies keeps flying instead of setting down where it stands", "[aircraftidle]")
    {
        // Reported from play: once the structure was destroyed the bomber
        // stopped in place and landed. The original does not end an attack
        // that way. AirStrike's prologue (0x411FF5) appends a VTOL_SeekAttack
        // carrying the aircraft's own position and deletes itself, and that
        // mission (0x4103E0) circles the spot indefinitely; the only thing
        // that sends it home is dropping below three quarters health with a
        // repair pad within reach.
        auto script = makeIdleScript();
        GameSimulation sim(makeIdleTerrain(256, 256), 0u, 0, 0);
        auto us = addIdlePlayer(sim, "us");
        auto them = addIdlePlayer(sim, "them");
        sim.unitDefinitions["bomber"] = makeIdleBomberDef();
        sim.unitDefinitions["target"] = makeIdleTargetDef();
        registerIdleModel(sim, "model");
        defineIdleBomb(sim);
        sim.losTables = generateLosTables(8);

        auto targetPosition = SimVector(0_ss, 0_ss, 0_ss);
        auto targetId = spawnIdleUnit(sim, "target", them, targetPosition, script);

        auto bomberId = spawnIdleUnit(sim, "bomber", us, SimVector(-900_ss, 200_ss, 0_ss), script);
        {
            auto& bomber = sim.getUnitState(bomberId);
            bomber.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "bomb";
            bomber.weapons[0] = weapon;
            bomber.orders.push_back(createAttackOrder(targetId));
        }
        sim.flyingUnitsSet.insert(bomberId);

        // Let it commit to a run, so the target dies while the aircraft is in
        // the middle of one. Handovers in this codebase have a habit of only
        // working from level flight, and this is the state it will really be
        // in when a bomb goes off.
        for (int tick = 0; tick < 120; ++tick)
        {
            sim.tick();
        }
        {
            const auto& b = sim.getUnitState(bomberId);
            const auto& air = std::get<UnitPhysicsInfoAir>(b.physics);
            REQUIRE(std::holds_alternative<AirMovementStateAttackRun>(air.movementState));
        }

        // Dead units are swept at the end of a tick, so it takes one more
        // before the attack handler sees the target gone.
        sim.getUnitState(targetId).markAsDeadNoCorpse();
        sim.tick();
        sim.tick();

        auto anchor = sim.getUnitState(bomberId).position;
        auto previous = anchor;
        auto travelled = 0_ss;
        auto everLanded = false;
        for (int tick = 0; tick < 600; ++tick)
        {
            sim.tick();
            const auto& b = sim.getUnitState(bomberId);
            everLanded = everLanded || isLanding(b) || std::holds_alternative<UnitPhysicsInfoGround>(b.physics);
            travelled = travelled + idleFlatDistance(previous, b.position);
            previous = b.position;
        }

        INFO("travelled " << simScalarToFloat(travelled) << " world units in the twenty seconds after the target died");
        REQUIRE_FALSE(everLanded);
        // Twenty seconds of a nine-a-tick aircraft is well over three thousand
        // units even allowing for the turn; anything that has stopped in place
        // cannot manage a few hundred.
        REQUIRE(travelled > 2000_ss);
        // And it has not simply flown off: the circuit keeps it near the spot.
        REQUIRE(idleFlatDistance(sim.getUnitState(bomberId).position, anchor) < 2000_ss);

        const auto& b = sim.getUnitState(bomberId);
        REQUIRE(b.airLoiter.has_value());
        REQUIRE(b.airLoiter->reason == UnitState::AirLoiterState::Reason::AttackEnded);
    }

    TEST_CASE("the circuit works its way round the dead target rather than orbiting or drifting off", "[aircraftidle]")
    {
        // VTOL_SeekAttack puts its goal a weapon range plus 160 out from the
        // anchor — 1440 for any of the four bombers, whose bombs all reach
        // 1280 — and steps the bearing back by a third of a turn plus up to
        // an eighth every time it arrives (0x410625, 0x41064B). The step is
        // more than a quarter turn and less than half of one, so each leg is
        // a chord that passes close to the middle. That is why the original's
        // bombers keep coming back over what they flattened.
        auto script = makeIdleScript();
        GameSimulation sim(makeIdleTerrain(256, 256), 0u, 0, 0);
        auto us = addIdlePlayer(sim, "us");
        auto them = addIdlePlayer(sim, "them");
        sim.unitDefinitions["bomber"] = makeIdleBomberDef();
        sim.unitDefinitions["target"] = makeIdleTargetDef();
        registerIdleModel(sim, "model");
        defineIdleBomb(sim);
        sim.losTables = generateLosTables(8);

        auto targetId = spawnIdleUnit(sim, "target", them, SimVector(0_ss, 0_ss, 0_ss), script);
        auto bomberId = spawnIdleUnit(sim, "bomber", us, SimVector(-400_ss, 200_ss, 0_ss), script);
        {
            auto& bomber = sim.getUnitState(bomberId);
            bomber.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "bomb";
            bomber.weapons[0] = weapon;
            bomber.orders.push_back(createAttackOrder(targetId));
        }
        sim.flyingUnitsSet.insert(bomberId);

        for (int tick = 0; tick < 60; ++tick)
        {
            sim.tick();
        }
        // Dead units are swept at the end of a tick, so it takes one more
        // before the attack handler sees the target gone.
        sim.getUnitState(targetId).markAsDeadNoCorpse();
        sim.tick();
        sim.tick();

        auto anchor = sim.getUnitState(bomberId).position;

        auto furthest = 0_ss;
        auto nearestAfterFurthest = 100000_ss;
        auto beenOut = false;
        for (int tick = 0; tick < 1800; ++tick)
        {
            sim.tick();
            auto d = idleFlatDistance(sim.getUnitState(bomberId).position, anchor);
            furthest = rweMax(furthest, d);
            if (d > 1200_ss)
            {
                beenOut = true;
            }
            if (beenOut)
            {
                nearestAfterFurthest = rweMin(nearestAfterFurthest, d);
            }
        }

        INFO("furthest " << simScalarToFloat(furthest) << ", nearest after reaching the ring " << simScalarToFloat(nearestAfterFurthest));
        // Out to the ring...
        REQUIRE(beenOut);
        REQUIRE(furthest > 1200_ss);
        // ...but never much past it: the goal is on the ring, not beyond it.
        REQUIRE(furthest < 1900_ss);
        // ...and back across the middle on the next leg.
        REQUIRE(nearestAfterFurthest < 900_ss);
    }

    TEST_CASE("an aircraft that merely runs out of orders still goes and lands", "[aircraftidle]")
    {
        // The other half of the answer, and the thing that must not regress.
        // A unit whose mission list simply empties gets its definition's
        // DefaultMissionType, which every one of the twenty-one shipped
        // aircraft names as VTOL_Standby (0x40F7D0). That handler hops about
        // the spot only while the aircraft has something attached to it
        // (unit+0x8A, tested at 0x40F823); with nothing aboard it pushes
        // VTOL_LandIfCan and sets down.
        auto script = makeIdleScript();
        GameSimulation sim(makeIdleTerrain(128, 128), 0u, 0, 0);
        auto us = addIdlePlayer(sim, "us");
        sim.unitDefinitions["bomber"] = makeIdleBomberDef();
        registerIdleModel(sim, "model");
        defineIdleBomb(sim);
        sim.losTables = generateLosTables(8);

        auto bomberId = spawnIdleUnit(sim, "bomber", us, SimVector(0_ss, 200_ss, 0_ss), script);
        {
            auto& bomber = sim.getUnitState(bomberId);
            bomber.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "bomb";
            bomber.weapons[0] = weapon;
            bomber.orders.push_back(createMoveOrder(SimVector(300_ss, 0_ss, 0_ss)));
        }
        sim.flyingUnitsSet.insert(bomberId);

        auto settled = false;
        for (int tick = 0; tick < 900 && !settled; ++tick)
        {
            sim.tick();
            const auto& b = sim.getUnitState(bomberId);
            settled = isLanding(b) || std::holds_alternative<UnitPhysicsInfoGround>(b.physics);
        }

        REQUIRE(settled);
        REQUIRE_FALSE(sim.getUnitState(bomberId).airLoiter.has_value());
    }

    TEST_CASE("a construction aircraft guarding an idle factory mills about instead of hanging still", "[aircraftidle]")
    {
        // Reported from play, as a question: do RWE's construction planes fly
        // around near an idle factory the way the original's do? They did not
        // — handleGuardOrder did nothing at all once the guard was within two
        // hundred units, so the aircraft coasted to a dead stop beside it.
        // The original's guard mission for an aircraft is VTOL_Follow
        // (0x40FBE0, chosen at 0x43F4C7), and with no work to copy from the
        // guarded unit it spends its time putting a goal on a ring around it
        // and stepping the bearing on arrival (0x41013B). For something with
        // no weapon the ring is 320 units — twenty tiles across (0x4101B0) —
        // so the plane dashes about the factory's neighbourhood.
        auto script = makeIdleScript();
        GameSimulation sim(makeIdleTerrain(128, 128), 0u, 0, 0);
        auto us = addIdlePlayer(sim, "us");
        sim.unitDefinitions["builder"] = makeIdleBuilderDef();
        sim.unitDefinitions["factory"] = makeIdleTargetDef();
        registerIdleModel(sim, "model");
        sim.losTables = generateLosTables(8);

        auto factoryPosition = SimVector(0_ss, 0_ss, 0_ss);
        auto factoryId = spawnIdleUnit(sim, "factory", us, factoryPosition, script);

        // Close enough that the old "stay within two hundred units" rule had
        // nothing to say, which is exactly the case that stood still.
        auto planeId = spawnIdleUnit(sim, "builder", us, SimVector(60_ss, 70_ss, 0_ss), script);
        {
            auto& plane = sim.getUnitState(planeId);
            plane.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            plane.orders.push_back(GuardOrder(factoryId));
        }
        sim.flyingUnitsSet.insert(planeId);

        // Give it a moment to get onto the ring before measuring.
        for (int tick = 0; tick < 60; ++tick)
        {
            sim.tick();
        }

        auto previous = sim.getUnitState(planeId).position;
        auto travelled = 0_ss;
        auto furthest = 0_ss;
        for (int tick = 0; tick < 600; ++tick)
        {
            sim.tick();
            const auto& p = sim.getUnitState(planeId);
            travelled = travelled + idleFlatDistance(previous, p.position);
            previous = p.position;
            furthest = rweMax(furthest, idleFlatDistance(p.position, factoryPosition));
        }

        INFO("travelled " << simScalarToFloat(travelled) << " world units, furthest from the factory " << simScalarToFloat(furthest));
        // It never stops. A plane parked beside the factory covers nothing.
        REQUIRE(travelled > 1000_ss);
        // But it stays in the factory's neighbourhood: the ring is 320 units
        // and the goal's arrival tolerance a hundred and twenty-eight.
        REQUIRE(furthest < 700_ss);
        REQUIRE(sim.getUnitState(planeId).airLoiter.has_value());
        REQUIRE(sim.getUnitState(planeId).airLoiter->reason == UnitState::AirLoiterState::Reason::Guarding);
    }

    TEST_CASE("a guard order dropped afterwards lets the aircraft land again", "[aircraftidle]")
    {
        // The guard's circuit belongs to the order, not to the aircraft: in
        // the original it lives in the mission and dies with it. Cancelling
        // the guard has to leave the plane free to go home, or an idle
        // construction aircraft would circle for ever.
        auto script = makeIdleScript();
        GameSimulation sim(makeIdleTerrain(128, 128), 0u, 0, 0);
        auto us = addIdlePlayer(sim, "us");
        sim.unitDefinitions["builder"] = makeIdleBuilderDef();
        sim.unitDefinitions["factory"] = makeIdleTargetDef();
        registerIdleModel(sim, "model");
        sim.losTables = generateLosTables(8);

        auto factoryId = spawnIdleUnit(sim, "factory", us, SimVector(0_ss, 0_ss, 0_ss), script);
        auto planeId = spawnIdleUnit(sim, "builder", us, SimVector(60_ss, 70_ss, 0_ss), script);
        {
            auto& plane = sim.getUnitState(planeId);
            plane.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            plane.orders.push_back(GuardOrder(factoryId));
        }
        sim.flyingUnitsSet.insert(planeId);

        for (int tick = 0; tick < 120; ++tick)
        {
            sim.tick();
        }
        REQUIRE(sim.getUnitState(planeId).airLoiter.has_value());

        sim.getUnitState(planeId).orders.clear();

        auto settled = false;
        for (int tick = 0; tick < 900 && !settled; ++tick)
        {
            sim.tick();
            const auto& p = sim.getUnitState(planeId);
            settled = isLanding(p) || std::holds_alternative<UnitPhysicsInfoGround>(p.physics);
        }

        REQUIRE(settled);
    }
}
