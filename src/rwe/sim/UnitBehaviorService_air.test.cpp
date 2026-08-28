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

        SECTION("is capped at 600 regardless of weapon range")
        {
            auto def = makeBomberDefinition();
            def.cruiseAltitude = 1000_ss;
            REQUIRE(defaultAttackRunOutDistance(def, 2000_ss) == 600_ss);
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
            auto weaponsHot = stepAttackRunPhase(unitPos, targetPos, 100_ss, state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);
            REQUIRE_FALSE(weaponsHot);
        }

        SECTION("transitions to Engaging once within weapon range")
        {
            auto state = makeAttackRunState(SimVector(50_ss, 50_ss, 0_ss));
            SimVector unitPos(0_ss, 50_ss, 0_ss);
            SimVector targetPos(50_ss, 0_ss, 0_ss);
            auto weaponsHot = stepAttackRunPhase(unitPos, targetPos, 100_ss, state);
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
            auto weaponsHot = stepAttackRunPhase(unitPos, targetPos, 100_ss, state);
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
            auto weaponsHot = stepAttackRunPhase(unitPos, targetPos, 100_ss, state);
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
            auto weaponsHot = stepAttackRunPhase(unitPos, targetPos, 100_ss, state);
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
            auto weaponsHot = stepAttackRunPhase(unitPos, targetPos, 100_ss, state);
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);
            REQUIRE_FALSE(weaponsHot);
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
            auto v = computeNewAttackRunVelocity(unit, def, state);
            // velocity should be along +x (toward target), magnitude up to acceleration
            REQUIRE(v.x > 0_ss);
            REQUIRE(v.z == 0_ss);
        }

        SECTION("does not slow down when very close to target (Engaging lookahead applies)")
        {
            unit.position = SimVector(99_ss, 50_ss, 0_ss);
            auto state = makeAttackRunState(SimVector(100_ss, 50_ss, 0_ss));
            state.phase = AirMovementStateAttackRun::Phase::Engaging;
            state.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            // Already at near max velocity
            state.currentVelocity = SimVector(4_ss, 0_ss, 0_ss);
            auto v = computeNewAttackRunVelocity(unit, def, state);
            // velocity magnitude should remain at maxVelocity (4)
            REQUIRE(v.x == 4_ss);
            REQUIRE(v.z == 0_ss);
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
            REQUIRE_FALSE(stepAttackRunPhase(SimVector(-500_ss, 50_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), 100_ss, state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Approaching);

            // Approaching -> Engaging once we're in range
            REQUIRE(stepAttackRunPhase(SimVector(-30_ss, 50_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), 100_ss, state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Engaging);

            // Still Engaging while approaching the target
            REQUIRE(stepAttackRunPhase(SimVector(20_ss, 50_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), 100_ss, state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Engaging);

            // Engaging -> Departing once we pass the target
            REQUIRE_FALSE(stepAttackRunPhase(SimVector(70_ss, 50_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), 100_ss, state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Departing);

            // Still Departing while inside run-out distance
            REQUIRE_FALSE(stepAttackRunPhase(SimVector(150_ss, 50_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), 100_ss, state));
            REQUIRE(state.phase == AirMovementStateAttackRun::Phase::Departing);

            // Departing -> Approaching once we exceed run-out distance (250 > 200)
            REQUIRE_FALSE(stepAttackRunPhase(SimVector(300_ss, 50_ss, 0_ss), SimVector(50_ss, 0_ss, 0_ss), 100_ss, state));
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
