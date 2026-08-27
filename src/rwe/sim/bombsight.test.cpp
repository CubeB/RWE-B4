#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitBehaviorService_util.h>

namespace rwe
{
    // Gravity used by ProjectilePhysicsTypeBallistic / ProjectilePhysicsTypeBomb
    // in updateProjectiles. Kept here as a reference for the test math; if the
    // sim ever changes the ballistic gravity, these tests will break in lock-
    // step with predictBombImpactPoint, which uses the same constant.
    static const SimScalar kBallisticGravityPerTickSquared = 112_ss / (30_ss * 30_ss);

    TEST_CASE("predictBombImpactPoint: stationary bomber overhead", "[bombsight]")
    {
        // A bomber sitting still directly over the target (no horizontal
        // velocity) should drop the bomb on its own XZ position.
        SimVector bomber(100_ss, 200_ss, 50_ss);
        SimVector velocity(0_ss, 0_ss, 0_ss);
        SimScalar groundY(0_ss);

        auto impact = predictBombImpactPoint(bomber, velocity, groundY);

        REQUIRE(impact.x == 100_ss);
        REQUIRE(impact.z == 50_ss);
        REQUIRE(impact.y == groundY);
    }

    TEST_CASE("predictBombImpactPoint: moving bomber lands ahead", "[bombsight]")
    {
        // With horizontal velocity, the bomb's predicted impact must lead the
        // bomber along the velocity vector by sqrt(2h/g) ticks worth of v_xz.
        SimVector bomber(0_ss, 200_ss, 0_ss);
        SimVector velocity(4_ss, 0_ss, 0_ss); // typical bomber max velocity in tests
        SimScalar groundY(0_ss);

        auto impact = predictBombImpactPoint(bomber, velocity, groundY);

        // h = 200, g = 112/900 -> 2h/g = 400 * 900 / 112 = 3214.28...
        // sqrt(3214.28) ~ 56.69. Impact x = 4 * 56.69 ~ 226.78.
        // Use rweSqrt internally for determinism; here just sanity-check
        // that the impact is well ahead of the bomber along +x.
        REQUIRE(impact.x > 100_ss);
        REQUIRE(impact.x < 400_ss);
        REQUIRE(impact.z == 0_ss);
        REQUIRE(impact.y == groundY);
    }

    TEST_CASE("predictBombImpactPoint: bomber at or below ground returns its own XZ", "[bombsight]")
    {
        // Degenerate case (bomber rendered as having driven into the floor).
        // Bombsight code must not divide by zero or take sqrt of negative.
        SECTION("bomber at exactly ground level")
        {
            SimVector bomber(50_ss, 0_ss, 25_ss);
            SimVector velocity(4_ss, 0_ss, 0_ss);
            auto impact = predictBombImpactPoint(bomber, velocity, 0_ss);
            REQUIRE(impact.x == 50_ss);
            REQUIRE(impact.z == 25_ss);
            REQUIRE(impact.y == 0_ss);
        }

        SECTION("bomber below ground")
        {
            SimVector bomber(50_ss, -5_ss, 25_ss);
            SimVector velocity(4_ss, 0_ss, 0_ss);
            auto impact = predictBombImpactPoint(bomber, velocity, 0_ss);
            REQUIRE(impact.x == 50_ss);
            REQUIRE(impact.z == 25_ss);
            REQUIRE(impact.y == 0_ss);
        }
    }

    TEST_CASE("predictBombImpactPoint: scales with altitude", "[bombsight]")
    {
        // Higher altitude -> longer fall time -> impact is farther forward.
        SimVector velocity(4_ss, 0_ss, 0_ss);
        auto lowImpact = predictBombImpactPoint(SimVector(0_ss, 100_ss, 0_ss), velocity, 0_ss);
        auto highImpact = predictBombImpactPoint(SimVector(0_ss, 400_ss, 0_ss), velocity, 0_ss);
        REQUIRE(highImpact.x > lowImpact.x);
    }

    TEST_CASE("bombsightInReleaseWindow: stationary target directly under bomber", "[bombsight]")
    {
        // Stationary bomber overhead a stationary target -> impact lands on
        // bomber XZ. If target is under bomber, we're in the window.
        SimVector bomber(100_ss, 200_ss, 50_ss);
        SimVector velocity(0_ss, 0_ss, 0_ss);
        SimVector target(100_ss, 0_ss, 50_ss);

        REQUIRE(bombsightInReleaseWindow(bomber, velocity, target, 16_ss));
    }

    TEST_CASE("bombsightInReleaseWindow: moving bomber must release early", "[bombsight]")
    {
        // A bomber flying forward will overshoot the target if it releases
        // when directly overhead. Release should fire while still short of
        // the target XZ, by roughly v * sqrt(2h/g).
        SimVector velocity(4_ss, 0_ss, 0_ss);
        SimScalar altitude(200_ss);

        // Target dead ahead at 0,0 ground level.
        SimVector target(0_ss, 0_ss, 0_ss);

        // Position bomber such that the predicted impact lands on the target.
        auto leadDistance = predictBombImpactPoint(SimVector(0_ss, altitude, 0_ss), velocity, 0_ss).x;
        SimVector bomber(-leadDistance, altitude, 0_ss);

        REQUIRE(bombsightInReleaseWindow(bomber, velocity, target, 16_ss));
    }

    TEST_CASE("bombsightInReleaseWindow: moving bomber not yet at release point", "[bombsight]")
    {
        // Same setup but the bomber hasn't reached the lead point yet.
        SimVector velocity(4_ss, 0_ss, 0_ss);
        SimScalar altitude(200_ss);
        SimVector target(0_ss, 0_ss, 0_ss);

        auto leadDistance = predictBombImpactPoint(SimVector(0_ss, altitude, 0_ss), velocity, 0_ss).x;
        // Place the bomber 200 units before the proper release point.
        SimVector bomber(-leadDistance - 200_ss, altitude, 0_ss);

        REQUIRE_FALSE(bombsightInReleaseWindow(bomber, velocity, target, 16_ss));
    }

    TEST_CASE("bombsightInReleaseWindow: moving bomber past release point", "[bombsight]")
    {
        // Bomber released too late: predicted impact overshoots the target.
        SimVector velocity(4_ss, 0_ss, 0_ss);
        SimScalar altitude(200_ss);
        SimVector target(0_ss, 0_ss, 0_ss);

        auto leadDistance = predictBombImpactPoint(SimVector(0_ss, altitude, 0_ss), velocity, 0_ss).x;
        // Bomber 200 units past where it should have released.
        SimVector bomber(-leadDistance + 200_ss, altitude, 0_ss);

        REQUIRE_FALSE(bombsightInReleaseWindow(bomber, velocity, target, 16_ss));
    }

    TEST_CASE("bombsightInReleaseWindow: lateral offset misses release window", "[bombsight]")
    {
        // Bomber flying parallel to but offset from the target line — even
        // when at the right "X" point, the Z miss distance puts impact
        // outside the release radius.
        SimVector velocity(4_ss, 0_ss, 0_ss);
        SimScalar altitude(200_ss);
        SimVector target(0_ss, 0_ss, 0_ss);

        auto leadDistance = predictBombImpactPoint(SimVector(0_ss, altitude, 0_ss), velocity, 0_ss).x;
        SimVector bomber(-leadDistance, altitude, 100_ss); // 100 units off in Z
        REQUIRE_FALSE(bombsightInReleaseWindow(bomber, velocity, target, 16_ss));
    }

    TEST_CASE("bombsightInReleaseWindow: release radius is honoured", "[bombsight]")
    {
        // Same lateral miss as above but with a generous release radius — should
        // pass. This documents that callers can tune the gate via radius.
        SimVector velocity(4_ss, 0_ss, 0_ss);
        SimScalar altitude(200_ss);
        SimVector target(0_ss, 0_ss, 0_ss);

        auto leadDistance = predictBombImpactPoint(SimVector(0_ss, altitude, 0_ss), velocity, 0_ss).x;
        SimVector bomber(-leadDistance, altitude, 100_ss);

        // 200 unit release radius easily encloses the 100-unit miss.
        REQUIRE(bombsightInReleaseWindow(bomber, velocity, target, 200_ss));
    }
}
