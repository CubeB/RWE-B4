#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <rwe/game/GameScene_util.h>

namespace rwe
{
    // The two periods the original passes: 16 for Wake1 and ReverseWake1, 8
    // for Wake2 and ReverseWake2.
    static const unsigned int Wake1Period = 16;
    static const unsigned int Wake2Period = 8;

    TEST_CASE("computeWakeEmission: a dot starts at the first vertex and drifts towards the second", "[wake]")
    {
        Vector3f first(10.0f, 0.0f, 0.0f);
        Vector3f second(110.0f, 0.0f, 0.0f);

        auto emission = computeWakeEmission(first, second, false, Wake1Period);

        REQUIRE(emission.spawnPosition.x == 10.0f);
        REQUIRE(emission.velocity.x == 0.5f);
        REQUIRE(emission.velocity.y == 0.0f);
        REQUIRE(emission.velocity.z == 0.0f);
    }

    TEST_CASE("computeWakeEmission: reverse swaps the two vertices and nothing else", "[wake]")
    {
        // That really is the whole of the difference between Wake1 and
        // ReverseWake1 in the original -- there is no test on the unit's
        // speed or direction of travel anywhere.
        Vector3f first(10.0f, 0.0f, 0.0f);
        Vector3f second(110.0f, 0.0f, 0.0f);

        auto forward = computeWakeEmission(first, second, false, Wake1Period);
        auto backward = computeWakeEmission(first, second, true, Wake1Period);

        REQUIRE(backward.spawnPosition.x == 110.0f);
        REQUIRE(backward.velocity.x == -forward.velocity.x);
        REQUIRE(backward.duration == forward.duration);
    }

    TEST_CASE("computeWakeEmission: the speed does not depend on how far apart the vertices are", "[wake]")
    {
        // The original normalises before scaling, so a modeller putting the
        // two vertices a long way apart does not make a faster wake.
        auto near = computeWakeEmission(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(2.0f, 0.0f, 0.0f), false, Wake1Period);
        auto far = computeWakeEmission(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(2000.0f, 0.0f, 0.0f), false, Wake1Period);

        REQUIRE(near.velocity.x == 0.5f);
        REQUIRE(far.velocity.x == 0.5f);
    }

    TEST_CASE("computeWakeEmission: coincident vertices give a dot that sits still", "[wake]")
    {
        // Normalising a zero vector would be a division by zero, and some
        // pieces really are degenerate.
        auto emission = computeWakeEmission(Vector3f(5.0f, 5.0f, 5.0f), Vector3f(5.0f, 5.0f, 5.0f), false, Wake1Period);

        REQUIRE(emission.velocity.x == 0.0f);
        REQUIRE(emission.velocity.y == 0.0f);
        REQUIRE(emission.velocity.z == 0.0f);
    }

    TEST_CASE("computeWakeEmission: the ramp period sets the life at six steps", "[wake]")
    {
        Vector3f first(0.0f, 0.0f, 0.0f);
        Vector3f second(1.0f, 0.0f, 0.0f);

        REQUIRE(computeWakeEmission(first, second, false, Wake1Period).duration == GameTime(96));
        REQUIRE(computeWakeEmission(first, second, false, Wake2Period).duration == GameTime(48));
    }

    TEST_CASE("computeWakeEmission: a wake travels half a unit a tick for its whole life", "[wake]")
    {
        // Wake1 lives 96 ticks at half a unit a tick, so it covers 48 world
        // units; Wake2 covers half that.
        auto wake1 = computeWakeEmission(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(1.0f, 0.0f, 0.0f), false, Wake1Period);
        REQUIRE(wake1.velocity.x * static_cast<float>(wake1.duration.value) == 48.0f);

        auto wake2 = computeWakeEmission(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(1.0f, 0.0f, 0.0f), false, Wake2Period);
        REQUIRE(wake2.velocity.x * static_cast<float>(wake2.duration.value) == 24.0f);
    }

    TEST_CASE("wakeColorIndex: the colour steps once every ramp period", "[wake]")
    {
        REQUIRE(wakeColorIndex(0, Wake1Period) == 0);
        REQUIRE(wakeColorIndex(15, Wake1Period) == 0);
        REQUIRE(wakeColorIndex(16, Wake1Period) == 1);
        REQUIRE(wakeColorIndex(31, Wake1Period) == 1);
        REQUIRE(wakeColorIndex(32, Wake1Period) == 2);

        // Wake2 walks the same seven colours twice as fast.
        REQUIRE(wakeColorIndex(8, Wake2Period) == 1);
        REQUIRE(wakeColorIndex(16, Wake2Period) == 2);
    }

    TEST_CASE("wakeColorIndex: the last colour comes up as the dot dies", "[wake]")
    {
        // Six steps over a life of 6 * period, so the deepest blue is reached
        // on the final tick and the ramp never has to wrap.
        REQUIRE(wakeColorIndex(6 * Wake1Period - 1, Wake1Period) == 5);
        REQUIRE(wakeColorIndex(6 * Wake1Period, Wake1Period) == 6);
        REQUIRE(wakeColorIndex(6 * Wake2Period, Wake2Period) == 6);
    }

    TEST_CASE("wakeColorIndex: it clamps rather than wrapping", "[wake]")
    {
        // The original would wrap back to the palest blue, but its life is
        // arranged so that it never gets the chance. Clamping keeps a dot
        // that outlives its schedule from flashing white.
        REQUIRE(wakeColorIndex(10000, Wake1Period) == 6);
        REQUIRE(wakeColorIndex(5, 0) == 0);
    }
}
