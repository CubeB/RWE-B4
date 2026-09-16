#include <catch2/catch_test_macros.hpp>
#include <rwe/AudioService.h>

namespace rwe
{
    namespace
    {
        // Two distinct identities to stand in for two different loaded
        // sounds -- selectTrackForSound never dereferences these, it only
        // compares them, so any two distinct addresses will do.
        int soundA;
        int soundB;
    }

    TEST_CASE("selectTrackForSound picks the first free unreserved track", "[audio]")
    {
        std::vector<TrackSlot> tracks{
            TrackSlot{false, nullptr}, // reserved
            TrackSlot{true, &soundA},
            TrackSlot{false, nullptr},
            TrackSlot{false, nullptr},
        };

        REQUIRE(selectTrackForSound(tracks, 1, &soundA, 4) == 2u);
    }

    TEST_CASE("selectTrackForSound never returns a reserved track", "[audio]")
    {
        std::vector<TrackSlot> tracks{
            TrackSlot{false, nullptr}, // reserved, and free -- must not be picked
            TrackSlot{true, &soundA},
            TrackSlot{true, &soundA},
        };

        REQUIRE(selectTrackForSound(tracks, 1, &soundA, 4) == std::nullopt);
    }

    TEST_CASE("selectTrackForSound returns nullopt when nothing unreserved is free", "[audio]")
    {
        std::vector<TrackSlot> tracks{
            TrackSlot{true, &soundA},
            TrackSlot{true, &soundB},
        };

        REQUIRE(selectTrackForSound(tracks, 0, &soundA, 4) == std::nullopt);
    }

    TEST_CASE("selectTrackForSound caps concurrent copies of the same sound", "[audio]")
    {
        // Three copies of soundA already playing, a free track sitting
        // right there -- with a cap of 3, a fourth copy is refused rather
        // than piling on. Never a steal: the free track is simply not handed
        // out.
        std::vector<TrackSlot> tracks{
            TrackSlot{true, &soundA},
            TrackSlot{true, &soundA},
            TrackSlot{true, &soundA},
            TrackSlot{false, nullptr},
        };

        REQUIRE(selectTrackForSound(tracks, 0, &soundA, 3) == std::nullopt);

        // One below the cap still gets a track.
        REQUIRE(selectTrackForSound(tracks, 0, &soundA, 4) == 3u);
    }

    TEST_CASE("selectTrackForSound's cap is per sound, not global", "[audio]")
    {
        // The pool is saturated with copies of soundA, but a first copy of
        // soundB is a different sample and gets to play.
        std::vector<TrackSlot> tracks{
            TrackSlot{true, &soundA},
            TrackSlot{true, &soundA},
            TrackSlot{true, &soundA},
            TrackSlot{true, &soundA},
        };

        REQUIRE(selectTrackForSound(tracks, 0, &soundA, 4) == std::nullopt);

        // No free track at all here, so this is really exercising that a
        // saturated soundA count doesn't block soundB's own count check.
        tracks.push_back(TrackSlot{false, nullptr});
        REQUIRE(selectTrackForSound(tracks, 0, &soundB, 4) == 4u);
    }

    TEST_CASE("selectTrackForSound ignores a stale key on a silent track", "[audio]")
    {
        // A track that finished still carries whatever sound it last played
        // in its slot (AudioService doesn't clear trackSoundKey on finish),
        // but it is not playing, so it must count as free, not as a copy.
        std::vector<TrackSlot> tracks{
            TrackSlot{false, &soundA},
        };

        REQUIRE(selectTrackForSound(tracks, 0, &soundA, 1) == 0u);
    }
}
