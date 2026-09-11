#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <vector>

/**
 * The original's BrakeRate nose re-aim, 0x43D38E-0x43D47D in TotalA.exe:
 * horizontal speed above BrakeRate is stripped and put back along the nose.
 * The numbers are the shipped rev31 ones -- ARMFIG and CORVAMP for the
 * fighters, ARMCA for the construction aircraft -- because the whole point
 * is what the rule does to a fast aircraft against a slow one, and a
 * fixture that invented its own would not show that.
 */
namespace rwe
{
    namespace
    {
        using Catch::Approx;

        UnitDefinition fighterFromFbi(SimScalar maxVelocity, SimScalar brakeRate, SimScalar acceleration, SimScalar turnRate)
        {
            UnitDefinition d{};
            d.isMobile = true;
            d.canMove = true;
            d.canFly = true;
            d.maxVelocity = maxVelocity;
            d.brakeRate = brakeRate;
            d.acceleration = acceleration;
            d.turnRate = turnRate;
            return d;
        }

        /** ARMFIG, the Freedom Fighter: MaxVelocity=10 BrakeRate=6 Acceleration=0.35 TurnRate=512. */
        UnitDefinition armfig() { return fighterFromFbi(10_ss, 6_ss, SimScalar(0.35f), 512_ss); }
        /** CORVAMP, the Vamp: MaxVelocity=12 BrakeRate=7 Acceleration=0.35 TurnRate=620. */
        UnitDefinition corvamp() { return fighterFromFbi(12_ss, 7_ss, SimScalar(0.35f), 620_ss); }
        /** ARMCA, the construction aircraft: MaxVelocity=6.9 BrakeRate=1.5 Acceleration=0.06 TurnRate=90. */
        UnitDefinition armca() { return fighterFromFbi(SimScalar(6.9f), SimScalar(1.5f), SimScalar(0.06f), 90_ss); }

        float horizontalSpeed(const SimVector& v)
        {
            return simScalarToFloat(SimVector(v.x, 0_ss, v.z).length());
        }

        /** Angle in degrees between the flight path and the nose, both flat. */
        float crabDegrees(const SimVector& velocity, const SimVector& nose)
        {
            SimVector v(velocity.x, 0_ss, velocity.z);
            SimVector n(nose.x, 0_ss, nose.z);
            auto cosine = simScalarToFloat(v.dot(n)) / (simScalarToFloat(v.length()) * simScalarToFloat(n.length()));
            cosine = std::max(-1.0f, std::min(1.0f, cosine));
            return std::acos(cosine) * 180.0f / 3.14159265f;
        }

        /**
         * Flies a fighter through a ninety-degree turn: it starts at speed
         * along +z with its nose on +z, and is then given a target due +x,
         * a long way off. The nose comes round at TurnRate a tick and the
         * velocity follows the profile, exactly as the flying state does it.
         * Returns the worst crab angle seen over the turn.
         */
        float worstCrabThroughTurn(const UnitDefinition& def)
        {
            auto script = makeEmptyCobScript();
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::make_unique<CobEnvironment>(script.get()));
            // A bare UnitState leaves its position uninitialised; the spawn
            // helpers always set it, and so must this.
            unit.position = SimVector(0_ss, 0_ss, 0_ss);
            unit.previousPosition = unit.position;
            unit.rotation = UnitState::toRotation(SimVector(0_ss, 0_ss, 1_ss));

            AirMovementStateFlying flying;
            flying.currentVelocity = SimVector(0_ss, 0_ss, def.maxVelocity);
            flying.targetPosition = SimVector(100000_ss, 0_ss, 0_ss);

            auto turnRateThisFrame = SimAngle(static_cast<uint16_t>(simScalarToFloat(def.turnRate)));

            float worst = 0.0f;
            for (int t = 0; t < 120; ++t)
            {
                auto direction = *flying.targetPosition - unit.position;
                unit.rotation = turnTowards(unit.rotation, UnitState::toRotation(direction), turnRateThisFrame);
                flying.currentVelocity = computeNewAirUnitVelocity(unit, def, flying);
                unit.position = unit.position + flying.currentVelocity;
                worst = std::max(worst, crabDegrees(flying.currentVelocity, UnitState::toDirection(unit.rotation)));
            }
            return worst;
        }
    }

    TEST_CASE("brake step: speed at or below BrakeRate is left exactly as it was", "[brakerate]")
    {
        auto nose = SimVector(0_ss, 0_ss, 1_ss);
        // ARMCA cruising at its own top speed sideways to its nose: 6.9 is
        // above 1.5, so this is the one construction aircraft case that
        // does change. Below it nothing does.
        auto slow = SimVector(1_ss, SimScalar(-0.5f), 1_ss);
        auto out = applyBrakeRateNoseReaim(slow, nose, SimScalar(1.5f));
        REQUIRE(simScalarToFloat(out.x) == Approx(1.0f));
        REQUIRE(simScalarToFloat(out.y) == Approx(-0.5f));
        REQUIRE(simScalarToFloat(out.z) == Approx(1.0f));

        auto exact = SimVector(6_ss, 0_ss, 0_ss);
        auto same = applyBrakeRateNoseReaim(exact, nose, 6_ss);
        REQUIRE(simScalarToFloat(same.x) == Approx(6.0f));
        REQUIRE(simScalarToFloat(same.z) == Approx(0.0f));
    }

    TEST_CASE("brake step: the excess above BrakeRate goes along the nose", "[brakerate]")
    {
        // A Freedom Fighter at its full 10 flying dead sideways to a nose on
        // +z: 6 stays on the flight path, the other 4 goes where it points.
        auto nose = SimVector(0_ss, 0_ss, 1_ss);
        auto out = applyBrakeRateNoseReaim(SimVector(10_ss, 0_ss, 0_ss), nose, 6_ss);
        REQUIRE(simScalarToFloat(out.x) == Approx(6.0f));
        REQUIRE(simScalarToFloat(out.z) == Approx(4.0f));

        SECTION("the vertical component is never touched")
        {
            auto climbing = applyBrakeRateNoseReaim(SimVector(10_ss, 3_ss, 0_ss), nose, 6_ss);
            REQUIRE(simScalarToFloat(climbing.y) == Approx(3.0f));
            REQUIRE(simScalarToFloat(climbing.x) == Approx(6.0f));
            REQUIRE(simScalarToFloat(climbing.z) == Approx(4.0f));
        }

        SECTION("flying where it points, nothing changes but the accounting")
        {
            auto straight = applyBrakeRateNoseReaim(SimVector(0_ss, 0_ss, 10_ss), nose, 6_ss);
            REQUIRE(simScalarToFloat(straight.x) == Approx(0.0f));
            REQUIRE(simScalarToFloat(straight.z) == Approx(10.0f));
        }

        SECTION("the re-aimed velocity is never faster than it came in")
        {
            REQUIRE(horizontalSpeed(out) < 10.0f);
            REQUIRE(horizontalSpeed(out) == Approx(std::sqrt(36.0f + 16.0f)));
        }
    }

    TEST_CASE("brake step: a fast fighter crabs less through a turn", "[brakerate]")
    {
        // The same ninety-degree turn, with the rule and with BrakeRate set
        // above MaxVelocity so it can never fire. The fighters' shipped
        // numbers keep their flight path within a few degrees of the nose;
        // without the rule the velocity lags the nose by a wide margin.
        auto withRule = armfig();
        auto without = armfig();
        without.brakeRate = 1000_ss;

        auto crabWith = worstCrabThroughTurn(withRule);
        auto crabWithout = worstCrabThroughTurn(without);

        REQUIRE(crabWithout > 20.0f);
        REQUIRE(crabWith < crabWithout / 2.0f);
        REQUIRE(crabWith < 15.0f);

        SECTION("and the Vamp, faster and tighter, the same")
        {
            auto vampWith = worstCrabThroughTurn(corvamp());
            auto vampWithout = corvamp();
            vampWithout.brakeRate = 1000_ss;
            REQUIRE(vampWith < worstCrabThroughTurn(vampWithout) / 2.0f);
        }

        SECTION("a construction aircraft is left much as it was")
        {
            // Its BrakeRate of 1.5 is a fifth of its top speed, but its
            // turn is so slow that the profile keeps velocity and nose
            // close anyway. The rule must not move it far.
            auto caWith = worstCrabThroughTurn(armca());
            auto caWithout = armca();
            caWithout.brakeRate = 1000_ss;
            REQUIRE(caWith <= worstCrabThroughTurn(caWithout) + 1.0f);
        }
    }
}
