#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    namespace
    {
        UnitDefinition makeBomberDefinition()
        {
            UnitDefinition def{};
            def.canFly = true;
            def.canMove = true;
            def.canAttack = true;
            def.isMobile = true;
            def.cruiseAltitude = 50_ss;
            def.maxVelocity = 4_ss;
            def.acceleration = 1_ss;
            def.brakeRate = 1_ss;
            def.turnRate = 1000_ss;
            def.maxHitPoints = 100;
            return def;
        }

        AirMovementStateAttackRun makeAttackRunState(const SimVector& target, SimAngle facing = SimAngle(0))
        {
            AirMovementStateAttackRun state(target);
            state.lastKnownTargetPos = target;
            state.runOutDirection = UnitState::toDirection(facing);
            state.runOutDistance = 200_ss;
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            return state;
        }

        /** The bomber's attack pattern for a weapon of the given range, with a run-out fixed at 200 for readable tests. */
        AttackRunGeometry makeGeometry(SimScalar weaponMaxRange)
        {
            auto geometry = computeAttackRunGeometry(makeBomberDefinition(), weaponMaxRange);
            geometry.runOutDistance = 200_ss;
            return geometry;
        }

        /** A heading pointing from the aircraft straight at the target: lined up for the run. */
        SimVector headingTowards(const SimVector& from, const SimVector& to)
        {
            SimVector heading(to.x - from.x, 0_ss, to.z - from.z);
            return heading.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
        }
    }

    TEST_CASE("attack run geometry comes from the aircraft's own stats", "[aircraft]")
    {
        auto def = makeBomberDefinition();

        SECTION("the turn radius is speed over turn rate")
        {
            // 4 units/tick at 1000 angle units/tick is a circle of about 42 units.
            auto radius = attackRunTurnRadius(def);
            REQUIRE(radius > 40_ss);
            REQUIRE(radius < 44_ss);

            // Half the turn rate, twice the circle.
            auto slow = def;
            slow.turnRate = 500_ss;
            REQUIRE(attackRunTurnRadius(slow) > radius * 1.9_ssf);
        }

        SECTION("a lumbering aircraft needs a longer run-out to come back round")
        {
            auto nimble = def;
            auto lumbering = def;
            lumbering.turnRate = 100_ss;
            lumbering.maxVelocity = 8_ss;
            REQUIRE(defaultAttackRunOutDistance(lumbering, 500_ss) > defaultAttackRunOutDistance(nimble, 500_ss));
        }
    }

    TEST_CASE("defaultAttackRunOutDistance", "[aircraft]")
    {
        SECTION("is about twice the cruise altitude, never shorter than 250")
        {
            auto def = makeBomberDefinition();
            def.cruiseAltitude = 0_ss;
            REQUIRE(defaultAttackRunOutDistance(def, 500_ss) == 250_ss);
            def.cruiseAltitude = 200_ss;
            REQUIRE(defaultAttackRunOutDistance(def, 100_ss) == 400_ss);
        }

        SECTION("is capped at 900 regardless of weapon range")
        {
            auto def = makeBomberDefinition();
            def.cruiseAltitude = 1000_ss;
            REQUIRE(defaultAttackRunOutDistance(def, 2000_ss) == 900_ss);
        }
    }

    TEST_CASE("computeAttackRunTargetPoint", "[aircraft]")
    {
        UnitState unit({}, std::unique_ptr<CobEnvironment>{});
        unit.position = SimVector(0_ss, 50_ss, 0_ss);

        auto def = makeBomberDefinition();

        SECTION("Approaching aims at target position")
        {
            auto state = makeAttackRunState(SimVector(100_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            auto pt = computeAttackRunTargetPoint(unit, def, state);
            REQUIRE(pt.x == 100_ss);
            REQUIRE(pt.z == 0_ss);
        }

        SECTION("Engaging aims along run-out direction (lookahead)")
        {
            auto state = makeAttackRunState(SimVector(100_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Engaging;
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            auto pt = computeAttackRunTargetPoint(unit, def, state);
            // unit.position + runOutDirection * (maxVelocity * 30) = (0,50,0) + (1,0,0)*120 = (120,50,0)
            REQUIRE(pt.x == 220_ss); // lastKnownTargetPos (100) + runOutDirection * maxVelocity * 30 (120)
            REQUIRE(pt.z == 0_ss);
        }

        SECTION("Departing aims past the target along run-out direction")
        {
            auto state = makeAttackRunState(SimVector(100_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Departing;
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            state.runOutDistance = 500_ss;
            auto pt = computeAttackRunTargetPoint(unit, def, state);
            // lastKnownTargetPos + runOutDirection * runOutDistance = (100,50,0) + (1,0,0)*500
            REQUIRE(pt.x == 600_ss);
            REQUIRE(pt.z == 0_ss);
        }
    }

    TEST_CASE("stepAttackRunPhase: Approaching to Engaging", "[aircraft]")
    {
        SECTION("stays Approaching while target is out of weapon range")
        {
            auto state = makeAttackRunState(SimVector(500_ss, 50_ss, 0_ss));
            SimVector unitPos(0_ss, 50_ss, 0_ss);
            SimVector targetPos(500_ss, 0_ss, 0_ss);
            auto weaponsHot = stepAttackRunPhase(unitPos, headingTowards(unitPos, targetPos), targetPos, makeGeometry(100_ss), state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);
            REQUIRE_FALSE(weaponsHot);
        }

        SECTION("transitions to Engaging once within weapon range")
        {
            auto state = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            SimVector unitPos(0_ss, 50_ss, 0_ss);
            SimVector targetPos(50_ss, 0_ss, 0_ss);
            auto weaponsHot = stepAttackRunPhase(unitPos, headingTowards(unitPos, targetPos), targetPos, makeGeometry(100_ss), state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Engaging);
            REQUIRE(weaponsHot);
        }
    }

    TEST_CASE("Engaging-phase weapons-hot window", "[aircraft]")
    {
        SECTION("weapons remain hot while still in Engaging (not yet past target)")
        {
            auto state = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Engaging;
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            // Unit hasn't passed the target yet (unit.x < target.x with runOutDirection +x)
            SimVector unitPos(40_ss, 50_ss, 0_ss);
            SimVector targetPos(50_ss, 0_ss, 0_ss);
            auto weaponsHot = stepAttackRunPhase(unitPos, headingTowards(unitPos, targetPos), targetPos, makeGeometry(100_ss), state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Engaging);
            REQUIRE(weaponsHot);
        }

        SECTION("weapons go cold when unit passes target along run-out vector")
        {
            auto state = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Engaging;
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            // Unit has passed: unit.x > target.x with runOutDirection +x
            SimVector unitPos(60_ss, 50_ss, 0_ss);
            SimVector targetPos(50_ss, 0_ss, 0_ss);
            auto weaponsHot = stepAttackRunPhase(unitPos, headingTowards(unitPos, targetPos), targetPos, makeGeometry(100_ss), state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Departing);
            REQUIRE_FALSE(weaponsHot);
        }
    }

    TEST_CASE("Departing back to Approaching (loop)", "[aircraft]")
    {
        SECTION("stays Departing while unit hasn't reached run-out distance")
        {
            auto state = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Departing;
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            state.runOutDistance = 200_ss;
            SimVector unitPos(100_ss, 50_ss, 0_ss);  // 50 units past target
            SimVector targetPos(50_ss, 0_ss, 0_ss);
            auto weaponsHot = stepAttackRunPhase(unitPos, headingTowards(unitPos, targetPos), targetPos, makeGeometry(100_ss), state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Departing);
            REQUIRE_FALSE(weaponsHot);
        }

        SECTION("loops back to Approaching once unit is past run-out distance")
        {
            auto state = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Departing;
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            state.runOutDistance = 200_ss;
            SimVector unitPos(300_ss, 50_ss, 0_ss);  // 250 units past target
            SimVector targetPos(50_ss, 0_ss, 0_ss);
            auto weaponsHot = stepAttackRunPhase(unitPos, headingTowards(unitPos, targetPos), targetPos, makeGeometry(100_ss), state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);
            REQUIRE_FALSE(weaponsHot);
        }
    }

    TEST_CASE("the strafing pass overshoots and breaks away", "[aircraft]")
    {
        // The original's `AirToGround` (0x412710) is not the bomber's run.
        // A gun-armed aircraft takes the target, overshoots to three weapon
        // ranges on the far side, breaks ninety degrees for one weapon range,
        // and comes round again -- holding its aim throughout, because the
        // handler never clears the weapon between states.

        SECTION("a gun holds its aim through the run-out, a bomber does not")
        {
            auto strafer = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            strafer.phase = AirMovementStateAttackRun::Phase::Departing;
            strafer.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            strafer.runOutDistance = 200_ss;
            strafer.strafingPass = true;
            SimVector unitPos(100_ss, 50_ss, 0_ss);
            SimVector targetPos(50_ss, 0_ss, 0_ss);
            REQUIRE(stepAttackRunPhase(unitPos, headingTowards(unitPos, targetPos), targetPos, makeGeometry(100_ss), strafer));

            auto bomber = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            bomber.phase = AirMovementStateAttackRun::Phase::Departing;
            bomber.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            bomber.runOutDistance = 200_ss;
            bomber.strafingPass = false;
            REQUIRE_FALSE(stepAttackRunPhase(unitPos, headingTowards(unitPos, targetPos), targetPos, makeGeometry(100_ss), bomber));
        }

        SECTION("the break flies to its waypoint and only then comes round")
        {
            auto state = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Breaking;
            state.strafingPass = true;
            state.breakWaypoint = SimVector(1000_ss, 50_ss, 0_ss);
            SimVector targetPos(50_ss, 0_ss, 0_ss);

            // Still a long way from the waypoint: keep breaking, guns live.
            SimVector farOff(300_ss, 50_ss, 0_ss);
            REQUIRE(stepAttackRunPhase(farOff, headingTowards(farOff, state.breakWaypoint), targetPos, makeGeometry(100_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Breaking);

            // Inside the original's arrival tolerance of 128: take the target again.
            SimVector arrived(1050_ss, 50_ss, 0_ss);
            stepAttackRunPhase(arrived, headingTowards(arrived, state.breakWaypoint), targetPos, makeGeometry(100_ss), state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);
        }

        SECTION("the break steers at its waypoint rather than the target")
        {
            UnitDefinition d{};
            d.maxVelocity = 5_ss;
            d.turnRate = 1000_ss;
            d.cruiseAltitude = 100_ss;

            UnitState unit({}, std::unique_ptr<CobEnvironment>{});
            unit.position = SimVector(0_ss, 100_ss, 0_ss);

            AirMovementStateAttackRun state(SimVector(500_ss, 0_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Breaking;
            state.strafingPass = true;
            state.lastKnownTargetPos = SimVector(500_ss, 100_ss, 0_ss);
            state.breakWaypoint = SimVector(0_ss, 100_ss, 700_ss);

            auto point = computeAttackRunTargetPoint(unit, d, state);
            REQUIRE(point.x == state.breakWaypoint.x);
            REQUIRE(point.z == state.breakWaypoint.z);
        }
    }

    TEST_CASE("an attack run manoeuvres instead of stalling", "[aircraft]")
    {
        UnitState unit({}, std::unique_ptr<CobEnvironment>{});
        auto def = makeBomberDefinition();

        SECTION("turning back for another pass keeps flying speed up")
        {
            // Flying east at full speed, told to come back for a target behind it.
            unit.position = SimVector(400_ss, 50_ss, 0_ss);
            auto state = makeAttackRunState(SimVector(0_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            state.currentVelocity = SimVector(4_ss, 0_ss, 0_ss);
            // Pointing the way it flies, as it would be in the game.
            unit.rotation = UnitState::toRotation(state.currentVelocity);

            // Fly the reversal out and check it never slows to a hover.
            auto slowest = state.currentVelocity.length();
            SimAngle turned(0);
            for (int tick = 0; tick < 200; ++tick)
            {
                state.currentVelocity = computeNewAttackRunVelocity(unit, def, state);
                unit.position = unit.position + state.currentVelocity;
                unit.rotation = UnitState::toRotation(state.currentVelocity);
                slowest = rweMin(slowest, state.currentVelocity.length());
                turned = angleBetween(SimAngle(0), UnitState::toRotation(state.currentVelocity));
                if (state.currentVelocity.x < 0_ss && rweAbs(state.currentVelocity.z) < 1_ss)
                {
                    break;
                }
            }

            // It came round to face west...
            REQUIRE(state.currentVelocity.x < 0_ss);
            // ...without ever dropping below its cruise speed on the way.
            REQUIRE(slowest >= 3_ss);
            (void)turned;
        }

        SECTION("the reversal is an arc no tighter than the aircraft's turn circle")
        {
            unit.position = SimVector(400_ss, 50_ss, 0_ss);
            auto startPosition = unit.position;
            auto state = makeAttackRunState(SimVector(0_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            state.currentVelocity = SimVector(4_ss, 0_ss, 0_ss);
            // Pointing the way it flies: with the nose left on its default
            // heading the brake step would veer the aircraft off along it on
            // the first tick and the reversal would be over in four.
            unit.rotation = UnitState::toRotation(state.currentVelocity);

            for (int tick = 0; tick < 200 && state.currentVelocity.x >= 0_ss; ++tick)
            {
                state.currentVelocity = computeNewAttackRunVelocity(unit, def, state);
                unit.position = unit.position + state.currentVelocity;
                // The nose follows the flight path, as updateUnitRotation
                // turns it; the brake step redirects speed above BrakeRate
                // along it, so a nose left on its default heading would
                // hold the aircraft on that heading for the whole loop.
                unit.rotation = UnitState::toRotation(state.currentVelocity);
            }

            // Half a circle of radius r displaces the aircraft about 2r sideways.
            auto radius = attackRunTurnRadius(def);
            auto sideways = rweAbs(unit.position.z - startPosition.z);
            REQUIRE(sideways > radius);
            REQUIRE(sideways < radius * 4_ss);
        }
    }

    TEST_CASE("an aircraft too close to turn onto the target extends away first", "[aircraft]")
    {
        SECTION("inside its own turn circle and badly lined up, it departs to gain room")
        {
            auto state = makeAttackRunState(SimVector(0_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            SimVector unitPos(20_ss, 50_ss, 0_ss);
            SimVector targetPos(0_ss, 0_ss, 0_ss);
            // Flying away from a target only 20 units off: it cannot turn that tightly.
            SimVector heading(1_ss, 0_ss, 0_ss);
            auto weaponsHot = stepAttackRunPhase(unitPos, heading, targetPos, makeGeometry(1000_ss), state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Departing);
            REQUIRE(state.runOutDirection.x == 1_ss);
            REQUIRE_FALSE(weaponsHot);
        }

        SECTION("lined up on a close target it commits to the run instead")
        {
            auto state = makeAttackRunState(SimVector(0_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            SimVector unitPos(-20_ss, 50_ss, 0_ss);
            SimVector targetPos(0_ss, 0_ss, 0_ss);
            SimVector heading(1_ss, 0_ss, 0_ss);
            REQUIRE(stepAttackRunPhase(unitPos, heading, targetPos, makeGeometry(1000_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Engaging);
        }

        SECTION("in range but crossing the target, it keeps manoeuvring rather than dropping")
        {
            auto state = makeAttackRunState(SimVector(0_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            // 300 units out (outside the turn circle), flying at right angles to the target.
            SimVector unitPos(-300_ss, 50_ss, 0_ss);
            SimVector targetPos(0_ss, 0_ss, 0_ss);
            SimVector heading(0_ss, 0_ss, 1_ss);
            REQUIRE_FALSE(stepAttackRunPhase(unitPos, heading, targetPos, makeGeometry(1000_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);
        }
    }

    TEST_CASE("computeNewAttackRunVelocity does not decelerate near target", "[aircraft]")
    {
        UnitState unit({}, std::unique_ptr<CobEnvironment>{});
        auto def = makeBomberDefinition();

        SECTION("accelerates from rest toward target")
        {
            unit.position = SimVector(0_ss, 50_ss, 0_ss);
            auto state = makeAttackRunState(SimVector(100_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            state.currentVelocity = SimVector(0_ss, 0_ss, 0_ss);
            // From a standstill the aircraft moves off along its nose, so point it at the target.
            unit.rotation = UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss));
            auto v = computeNewAttackRunVelocity(unit, def, state);
            REQUIRE(v.x > 0_ss);
            REQUIRE(v.x <= def.acceleration);
            REQUIRE(v.z * v.z < SimScalar(0.0001f)); // heading is +x up to fixed-point rounding
        }

        SECTION("from a standstill facing the wrong way it turns rather than sliding sideways")
        {
            unit.position = SimVector(0_ss, 50_ss, 0_ss);
            unit.rotation = SimAngle(0); // facing +z, target is at +x
            auto state = makeAttackRunState(SimVector(100_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Approaching;
            state.currentVelocity = SimVector(0_ss, 0_ss, 0_ss);
            auto v = computeNewAttackRunVelocity(unit, def, state);
            // Mostly along the nose, having swung one tick's worth towards the target.
            REQUIRE(v.z > v.x);
            REQUIRE(v.x > 0_ss);
        }

        SECTION("does not slow down when very close to target (Engaging lookahead applies)")
        {
            unit.position = SimVector(99_ss, 50_ss, 0_ss);
            auto state = makeAttackRunState(SimVector(100_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Engaging;
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            // Already at max velocity, and pointing the way it flies: the
            // nose follows the run's flight path in updateUnitRotation, and
            // the brake step redirects speed above BrakeRate along it.
            state.currentVelocity = SimVector(4_ss, 0_ss, 0_ss);
            unit.rotation = UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss));
            auto v = computeNewAttackRunVelocity(unit, def, state);
            // velocity magnitude should remain at maxVelocity (4)
            REQUIRE(v.length() > 3.99_ssf);
            REQUIRE(v.length() <= 4_ss);
            REQUIRE(v.z * v.z < SimScalar(0.0001f)); // heading is +x up to fixed-point rounding
        }
    }

    TEST_CASE("AttackRun state machine round-trip across many ticks", "[aircraft]")
    {
        SECTION("a full bombing run sequences Approaching -> Engaging -> Departing -> Approaching")
        {
            auto state = makeAttackRunState(SimVector(50_ss, 0_ss, 0_ss));
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            state.runOutDistance = 200_ss;

            // Approaching, far out
            REQUIRE_FALSE(stepAttackRunPhase(SimVector(-500_ss, 50_ss, 0_ss), SimVector(1_ss, 0_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), makeGeometry(100_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);

            // Approaching -> Engaging once we're in range
            REQUIRE(stepAttackRunPhase(SimVector(-30_ss, 50_ss, 0_ss), SimVector(1_ss, 0_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), makeGeometry(100_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Engaging);

            // Still Engaging while approaching the target
            REQUIRE(stepAttackRunPhase(SimVector(20_ss, 50_ss, 0_ss), SimVector(1_ss, 0_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), makeGeometry(100_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Engaging);

            // Engaging -> Departing once we pass the target
            REQUIRE_FALSE(stepAttackRunPhase(SimVector(70_ss, 50_ss, 0_ss), SimVector(1_ss, 0_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), makeGeometry(100_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Departing);

            // Still Departing while inside run-out distance
            REQUIRE_FALSE(stepAttackRunPhase(SimVector(150_ss, 50_ss, 0_ss), SimVector(1_ss, 0_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), makeGeometry(100_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Departing);

            // Departing -> Approaching once we exceed run-out distance (250 > 200)
            REQUIRE_FALSE(stepAttackRunPhase(SimVector(300_ss, 50_ss, 0_ss), SimVector(1_ss, 0_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), makeGeometry(100_ss), state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);
        }
    }

    TEST_CASE("isFlying recognizes AttackRun as flying", "[aircraft]")
    {
        UnitPhysicsInfoAir air;
        air.movementState = AirMovementStateAttackRun(SimVector(0_ss, 0_ss, 0_ss));
        UnitPhysicsInfo physics = air;
        REQUIRE(isFlying(physics));
    }

    TEST_CASE("AirMovementState variant termination back to Flying", "[aircraft]")
    {
        // Simulating the exit path: when the target is gone or order is done,
        // the unit's movementState should be switched from AttackRun to Flying.
        SECTION("Replacing AttackRun with Flying yields a Flying-typed variant")
        {
            UnitPhysicsInfoAir air;
            air.movementState = AirMovementStateAttackRun(SimVector(50_ss, 0_ss, 0_ss));
            REQUIRE(std::holds_alternative<AirMovementStateAttackRun>(air.movementState));

            // This mirrors what attackTargetAir does on target loss.
            air.movementState = AirMovementStateFlying();

            REQUIRE(std::holds_alternative<AirMovementStateFlying>(air.movementState));
            REQUIRE_FALSE(std::holds_alternative<AirMovementStateAttackRun>(air.movementState));
        }
    }
}
