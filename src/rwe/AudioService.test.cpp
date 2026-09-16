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

    TEST_CASE("computeEffectGain folds the base gain back into the channel volume", "[audio]")
    {
        // SDL2_mixer multiplied the sample's own level by the channel's.
        // SDL3_mixer has one gain per track and setting it replaces what was
        // there, so the base gain has to be folded in here or it is simply
        // lost -- which is what made the mix four times hotter than it was
        // ever meant to be (issue #58).
        //
        // Every figure here is a power of two, so the arithmetic is exact
        // and these can be compared directly.
        REQUIRE(computeEffectGain(128, 0.25f, 1.0f, true) == 0.25f);
        REQUIRE(computeEffectGain(64, 0.25f, 1.0f, true) == 0.125f);
        REQUIRE(computeEffectGain(0, 0.25f, 1.0f, true) == 0.0f);
    }

    TEST_CASE("computeEffectGain honours the volume setting and Sound Mode Off", "[audio]")
    {
        // Every other play path applies both, and this one is reapplied to
        // each unit sound channel every frame, so leaving them out here let
        // weapon fire ignore the slider and the mute outright.
        REQUIRE(computeEffectGain(128, 0.25f, 0.5f, true) == 0.125f);
        REQUIRE(computeEffectGain(128, 0.25f, 0.0f, true) == 0.0f);
        REQUIRE(computeEffectGain(128, 0.25f, 1.0f, false) == 0.0f);
    }

    TEST_CASE("computeEffectGain keeps the loudest allowed mix in range", "[audio]")
    {
        // computeSoundCeiling counts in units of one sound at the base gain
        // and allows at most four of them, so the loudest the mix may sum to
        // is four times the base gain: 2.0 at a half -- hot on purpose,
        // since uncorrelated peaks rarely land together -- where before the
        // base gain was folded in at all it was 8.0 and the mixer could only
        // clamp.
        auto loudestOneTrack = computeEffectGain(128, 0.5f, 1.0f, true);
        REQUIRE(loudestOneTrack <= 1.0f);
        REQUIRE((loudestOneTrack * 4.0f) == 2.0f);

        // A volume above the scale's top cannot push a track past it.
        REQUIRE(computeEffectGain(1000, 0.5f, 1.0f, true) == 0.5f);
    }
}
