#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitBehaviorService_util.h>

namespace rwe
{
    // The four long-range guns the accuracy work was checked against, straight
    // out of the shipped weapon files. The Vulcan and the Buzzsaw live in
    // ccdata.ccx; the other two are retail.
    static const SimAngle VulcanAccuracy(800);
    static const SimAngle BerthaAccuracy(500);
    static const SimAngle IntimidatorAccuracy(1000);

    TEST_CASE("computeAccuracyCone: an undamaged unit gets exactly its weapon's accuracy", "[accuracy]")
    {
        // The health term is (health << 11) / maxdamage, which is 0x800 on the
        // nose at full health, and the original adds 0x800 straight back. So a
        // fresh shooter fires the cone the TDF asked for and nothing else --
        // this is the case that pins the whole formula down.
        REQUIRE(computeAccuracyCone(VulcanAccuracy, 1400, 1400, 0) == VulcanAccuracy);
        REQUIRE(computeAccuracyCone(BerthaAccuracy, 1800, 1800, 0) == BerthaAccuracy);
        REQUIRE(computeAccuracyCone(IntimidatorAccuracy, 1900, 1900, 0) == IntimidatorAccuracy);
    }

    TEST_CASE("computeAccuracyCone: a hurt unit shoots wider", "[accuracy]")
    {
        // Half health leaves half of the 0x800 behind, so the cone opens by
        // 1024. A Vulcan on 700 of 1400 goes from 800 to 1824, which is a bit
        // over ten degrees of spread.
        REQUIRE(computeAccuracyCone(VulcanAccuracy, 700, 1400, 0) == SimAngle(1824));

        // A quarter health leaves three quarters of it: 800 + 2048 - 512.
        REQUIRE(computeAccuracyCone(VulcanAccuracy, 350, 1400, 0) == SimAngle(2336));

        // All but dead is the worst the original can do: the whole 0x800,
        // an eighth of a turn, on top of whatever the weapon asked for.
        REQUIRE(computeAccuracyCone(VulcanAccuracy, 0, 1400, 0) == SimAngle(800 + 0x800));
    }

    TEST_CASE("computeAccuracyCone: kills tighten the cone, but only past six", "[accuracy]")
    {
        // veterancy = kills / 3, and it only divides when that is more than 1.
        for (unsigned int kills = 0; kills < 6; ++kills)
        {
            REQUIRE(computeAccuracyCone(VulcanAccuracy, 1400, 1400, kills) == VulcanAccuracy);
        }

        REQUIRE(computeAccuracyCone(VulcanAccuracy, 1400, 1400, 6) == SimAngle(400));
        REQUIRE(computeAccuracyCone(VulcanAccuracy, 1400, 1400, 9) == SimAngle(266));
        REQUIRE(computeAccuracyCone(VulcanAccuracy, 1400, 1400, 30) == SimAngle(80));

        // It is an integer divide with no floor under it, so a preposterous
        // kill count really does close the cone completely and the weapon
        // stops straying at all. Nothing in a game ever gets there, but it is
        // what the original's idiv does and we match it rather than clamping.
        REQUIRE(computeAccuracyCone(VulcanAccuracy, 1400, 1400, 30000) == SimAngle(0));
    }

    TEST_CASE("computeAccuracyCone: damage and veterancy compose the way the original composes them", "[accuracy]")
    {
        // The health term goes on first and the divide second, so a hurt
        // veteran is not the same as a healthy one: 800 + 1024 = 1824, then
        // divided by three.
        REQUIRE(computeAccuracyCone(VulcanAccuracy, 700, 1400, 9) == SimAngle(1824 / 3));
    }

    TEST_CASE("computeAccuracyCone: a perfect weapon on a damaged unit still strays", "[accuracy]")
    {
        // accuracy=0 means "100%" in the data file's own words, but the health
        // term is added regardless, so even a perfect weapon spreads once its
        // mount has been shot up. That falls out of the original's arithmetic
        // rather than being a decision of ours.
        REQUIRE(computeAccuracyCone(SimAngle(0), 1000, 1000, 0) == SimAngle(0));
        REQUIRE(computeAccuracyCone(SimAngle(0), 500, 1000, 0) == SimAngle(1024));
    }

    TEST_CASE("applyAimError: zero error leaves the direction alone", "[accuracy]")
    {
        auto direction = toDirection(SimAngle(1234), SimAngle(2345));
        auto result = applyAimError(direction, SimAngle(0), SimAngle(0));

        REQUIRE(std::abs((result.x - direction.x).value) < 0.001f);
        REQUIRE(std::abs((result.y - direction.y).value) < 0.001f);
        REQUIRE(std::abs((result.z - direction.z).value) < 0.001f);
    }

    TEST_CASE("applyAimError: a heading error swings the shot sideways and leaves the elevation alone", "[accuracy]")
    {
        // Due north and flat.
        SimVector direction(0_ss, 0_ss, 1_ss);

        auto result = applyAimError(direction, SimAngle(4096), SimAngle(0));

        // A sixteenth of a turn is 22.5 degrees, so the shot should have swung
        // well off the z axis while staying in the horizontal plane.
        REQUIRE(std::abs(result.y.value) < 0.001f);
        REQUIRE(result.x > 0.3_ssf);
        REQUIRE(result.z > 0.9_ssf);

        // Still a unit vector -- the spawn multiplies this by the weapon
        // velocity, so a direction that grew or shrank would change the speed.
        auto length = result.length();
        REQUIRE(std::abs((length - 1_ss).value) < 0.001f);
    }

    TEST_CASE("applyAimError: a pitch error lifts or drops the shot", "[accuracy]")
    {
        SimVector direction(0_ss, 0_ss, 1_ss);

        auto up = applyAimError(direction, SimAngle(0), SimAngle(4096));
        REQUIRE(up.y > 0.3_ssf);
        REQUIRE(std::abs(up.x.value) < 0.001f);

        auto down = applyAimError(direction, SimAngle(0), SimAngle(0) - SimAngle(4096));
        REQUIRE(down.y < -0.3_ssf);
        REQUIRE(std::abs(down.x.value) < 0.001f);
    }

    TEST_CASE("applyAimError: the two errors are independent", "[accuracy]")
    {
        // The original draws the heading error and the pitch error separately,
        // so the spread is a rectangle in (heading, pitch), not a circular
        // cone. Applying both at once must give the same answer as applying
        // them one at a time.
        SimVector direction(0_ss, 0_ss, 1_ss);

        auto both = applyAimError(direction, SimAngle(3000), SimAngle(2000));
        auto staged = applyAimError(applyAimError(direction, SimAngle(3000), SimAngle(0)), SimAngle(0), SimAngle(2000));

        REQUIRE(std::abs((both.x - staged.x).value) < 0.001f);
        REQUIRE(std::abs((both.y - staged.y).value) < 0.001f);
        REQUIRE(std::abs((both.z - staged.z).value) < 0.001f);
    }

    TEST_CASE("applyAimError: a straight-up shot has no heading to perturb", "[accuracy]")
    {
        // A vertically launched missile has no horizontal component, so
        // atan2 has nothing to work with. It must come back unharmed rather
        // than as a NaN.
        SimVector direction(0_ss, 1_ss, 0_ss);
        auto result = applyAimError(direction, SimAngle(5000), SimAngle(5000));

        REQUIRE(result.x == 0_ss);
        REQUIRE(result.y == 1_ss);
        REQUIRE(result.z == 0_ss);
    }

    TEST_CASE("accuracy: the Vulcan's spread at its own range is worth hundreds of units", "[accuracy]")
    {
        // The point of the whole exercise. The Vulcan has range 3080 and
        // accuracy 800, so the heading error alone is uniform on +/-400 of
        // 65536 -- a little under 2.2 degrees -- and at that range it throws
        // the fall of shot well outside the weapon's own 100-unit blast.
        auto cone = computeAccuracyCone(VulcanAccuracy, 1400, 1400, 0);
        auto halfCone = SimAngle(cone.value / 2u);

        SimVector direction(0_ss, 0_ss, 1_ss);
        auto worst = applyAimError(direction, halfCone, SimAngle(0));

        // Where a shot at full deflection lands, 3080 units downrange.
        auto lateral = worst.x * 3080_ss;
        REQUIRE(lateral > 100_ss);
        REQUIRE(lateral < 180_ss);
    }
}
