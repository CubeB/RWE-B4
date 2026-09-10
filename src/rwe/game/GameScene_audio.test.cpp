#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

namespace rwe
{
    TEST_CASE("shouldStartNextMusicTrack: silence past the hold-off gets a track", "[music]")
    {
        REQUIRE(shouldStartNextMusicTrack(false, false, GameTime(600u), GameTime(0u)));
    }

    TEST_CASE("shouldStartNextMusicTrack: a playing track is left alone", "[music]")
    {
        REQUIRE(!shouldStartNextMusicTrack(false, true, GameTime(600u), GameTime(0u)));
    }

    TEST_CASE("shouldStartNextMusicTrack: the hold-off holds, up to the tick it names", "[music]")
    {
        // Coming down out of battle books four seconds of quiet, so the gate
        // has to still be shut on the last tick of them and open on the one
        // after.
        REQUIRE(!shouldStartNextMusicTrack(false, false, GameTime(119u), GameTime(120u)));
        REQUIRE(shouldStartNextMusicTrack(false, false, GameTime(120u), GameTime(120u)));
    }

    TEST_CASE("shouldStartNextMusicTrack: a game on its way out starts nothing", "[music]")
    {
        // The reason the flag exists (fork issue #13). Every exit from the
        // in-game menu stops the music and then hands the scene manager the
        // next scene, but the swap waits for the top of the next frame, so
        // this driver runs once more with the audio service silent -- exactly
        // the state it otherwise reads as "the last track ended, put another
        // one on", over the top of whatever the incoming scene starts.
        REQUIRE(!shouldStartNextMusicTrack(true, false, GameTime(600u), GameTime(0u)));

        // And it outranks every other reason to start one, including a
        // hold-off that has long since expired.
        REQUIRE(!shouldStartNextMusicTrack(true, false, GameTime(4000u), GameTime(120u)));
    }
}
