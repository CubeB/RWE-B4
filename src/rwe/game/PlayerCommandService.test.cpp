#include <catch2/catch_test_macros.hpp>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/game/RoundTripWindow.h>
#include <rwe/sim/SimTicksPerSecond.h>

namespace rwe
{
    TEST_CASE("commandBufferTargetForRttMillis")
    {
        SECTION("a low-latency link needs only a couple of ticks")
        {
            REQUIRE(commandBufferTargetForRttMillis(0.0f, 0.0f) == 2);
            REQUIRE(commandBufferTargetForRttMillis(1.0f, 0.0f) == 2);
            REQUIRE(commandBufferTargetForRttMillis(1.0f, 1.0f) == 2);
        }

        SECTION("an eighty millisecond link with ten milliseconds of jitter is four to five ticks")
        {
            auto target = commandBufferTargetForRttMillis(80.0f, 5.0f);
            REQUIRE(target >= 4);
            REQUIRE(target <= 5);
        }

        SECTION("grows with the average round trip")
        {
            REQUIRE(commandBufferTargetForRttMillis(160.0f, 0.0f) > commandBufferTargetForRttMillis(80.0f, 0.0f));
        }

        SECTION("grows with the deviation, which is what a jittery link needs")
        {
            auto steady = commandBufferTargetForRttMillis(80.0f, 0.0f);
            auto jittery = commandBufferTargetForRttMillis(80.0f, 15.0f);
            REQUIRE(jittery > steady);
        }

        SECTION("is unchanged by a negative sample, and has a ceiling")
        {
            REQUIRE(commandBufferTargetForRttMillis(-5.0f, -5.0f) == commandBufferTargetForRttMillis(0.0f, 0.0f));
            auto huge = commandBufferTargetForRttMillis(100000.0f, 100000.0f);
            REQUIRE(huge == 82);
        }
    }

    TEST_CASE("updateRoundTripDeviation grows fast and shrinks slowly")
    {
        // A spike moves the deviation a quarter of the way in one sample.
        auto afterSpike = updateRoundTripDeviation(0.0f, 200.0f, 100.0f);
        REQUIRE(afterSpike == 25.0f);

        // Coming back to the average only walks it down by three quarters a
        // sample, so it is still most of the way up after one good sample.
        auto afterOneGoodSample = updateRoundTripDeviation(afterSpike, 100.0f, 100.0f);
        REQUIRE(afterOneGoodSample == 18.75f);

        auto afterTwo = updateRoundTripDeviation(afterOneGoodSample, 100.0f, 100.0f);
        auto afterThree = updateRoundTripDeviation(afterTwo, 100.0f, 100.0f);
        REQUIRE(afterTwo < afterOneGoodSample);
        REQUIRE(afterThree < afterTwo);

        // A steady average leaves it at zero.
        REQUIRE(updateRoundTripDeviation(0.0f, 100.0f, 100.0f) == 0.0f);
    }
}
