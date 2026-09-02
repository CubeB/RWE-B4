#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

namespace rwe
{
    TEST_CASE("accumulateScreenShake: a first shake halves the duration it asked for", "[screenshake]")
    {
        // The original reads the running duration before writing it and never
        // clears it, so a shake starting from a standing start is averaged
        // against zero. Two seconds of shakeduration becomes one.
        ScreenShakeState state;
        accumulateScreenShake(state, 50, 60);

        REQUIRE(state.running);
        REQUIRE(state.totalTicks == 30);
        REQUIRE(state.ticksRemaining == 30);
        REQUIRE(state.magnitudeX == 50);
        REQUIRE(state.magnitudeY == 50);
    }

    TEST_CASE("accumulateScreenShake: magnitudes add and durations average", "[screenshake]")
    {
        // Two shells landing together hit twice as hard, but the shake does
        // not last twice as long -- 0x41C685 takes the mean.
        ScreenShakeState state;
        accumulateScreenShake(state, 50, 60);
        REQUIRE(state.totalTicks == 30);

        accumulateScreenShake(state, 50, 60);
        REQUIRE(state.magnitudeX == 100);
        REQUIRE(state.magnitudeY == 100);
        REQUIRE(state.totalTicks == 45);
        REQUIRE(state.ticksRemaining == 45);
    }

    TEST_CASE("accumulateScreenShake: a shake that has finished starts its magnitude over", "[screenshake]")
    {
        ScreenShakeState state;
        accumulateScreenShake(state, 50, 60);
        state.running = false;

        accumulateScreenShake(state, 20, 60);
        REQUIRE(state.magnitudeX == 20);
        REQUIRE(state.magnitudeY == 20);
    }

    TEST_CASE("screenShakeAmplitudes: the ramp falls linearly to nothing", "[screenshake]")
    {
        ScreenShakeState state;
        state.running = true;
        state.magnitudeX = 100;
        state.magnitudeY = 100;
        state.totalTicks = 100;

        state.ticksRemaining = 100;
        REQUIRE(screenShakeAmplitudes(state).first == 100);

        state.ticksRemaining = 50;
        REQUIRE(screenShakeAmplitudes(state).first == 50);

        state.ticksRemaining = 10;
        REQUIRE(screenShakeAmplitudes(state).first == 10);

        state.ticksRemaining = 0;
        REQUIRE(screenShakeAmplitudes(state).first == 0);
    }

    TEST_CASE("screenShakeAmplitudes: nothing shakes when nothing is running", "[screenshake]")
    {
        ScreenShakeState state;
        REQUIRE(screenShakeAmplitudes(state).first == 0);
        REQUIRE(screenShakeAmplitudes(state).second == 0);
    }

    TEST_CASE("advanceScreenShake: counts down and then retires", "[screenshake]")
    {
        ScreenShakeState state;
        accumulateScreenShake(state, 50, 4);
        REQUIRE(state.totalTicks == 2);

        advanceScreenShake(state);
        REQUIRE(state.ticksRemaining == 1);
        REQUIRE(state.running);

        advanceScreenShake(state);
        REQUIRE(state.ticksRemaining == 0);
        REQUIRE(state.running);

        // The original clears the running bit on the frame it finds the
        // countdown already at zero, not on the frame it reaches zero.
        advanceScreenShake(state);
        REQUIRE_FALSE(state.running);
    }

    TEST_CASE("screenshake: a Big Bertha shell runs its whole shake down", "[screenshake]")
    {
        // A weapon with shakemagnitude 50 and one second of shakeduration.
        // Walking it frame by frame, the amplitude must start at its full
        // value, never grow, and reach nothing by the end.
        ScreenShakeState state;
        accumulateScreenShake(state, 50, 30);

        auto previous = screenShakeAmplitudes(state).first;
        REQUIRE(previous == 50);

        int frames = 0;
        while (state.running && frames < 1000)
        {
            auto amplitude = screenShakeAmplitudes(state).first;
            REQUIRE(amplitude <= previous);
            REQUIRE(amplitude >= 0);
            previous = amplitude;
            advanceScreenShake(state);
            ++frames;
        }

        REQUIRE_FALSE(state.running);
        REQUIRE(screenShakeAmplitudes(state).first == 0);

        // Half of the 30 ticks it asked for, plus the frame that retires it.
        REQUIRE(frames == 16);
    }
}
