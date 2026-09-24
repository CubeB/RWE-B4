#include <catch2/catch_test_macros.hpp>
#include <rwe/GlobalConfig.h>
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

    TEST_CASE("the track modes pick from the whole album", "[music]")
    {
        const std::vector<std::string> album{"music/02.mp3", "music/03.mp3", "music/04.mp3"};

        SECTION("Play All goes one on and wraps")
        {
            CHECK(nextMusicTrackIndex(MusicTrackMode::PlayAll, album, "", 0, 0) == 0u);
            CHECK(nextMusicTrackIndex(MusicTrackMode::PlayAll, album, "music/02.mp3", 0, 0) == 1u);
            CHECK(nextMusicTrackIndex(MusicTrackMode::PlayAll, album, "music/04.mp3", 0, 0) == 0u);
            CHECK(nextMusicTrackIndex(MusicTrackMode::PlayAll, album, "music/02.mp3", -1, 0) == 2u);
        }

        SECTION("Repeat stays on the track unless stepped")
        {
            CHECK(nextMusicTrackIndex(MusicTrackMode::Repeat, album, "music/03.mp3", 0, 0) == 1u);
            CHECK(nextMusicTrackIndex(MusicTrackMode::Repeat, album, "music/03.mp3", 1, 0) == 2u);
            CHECK(nextMusicTrackIndex(MusicTrackMode::Repeat, album, "music/02.mp3", -1, 0) == 2u);
            CHECK(nextMusicTrackIndex(MusicTrackMode::Repeat, album, "", 0, 0) == 0u);
        }

        SECTION("Random takes any track")
        {
            CHECK(nextMusicTrackIndex(MusicTrackMode::Random, album, "music/02.mp3", 0, 7) == 1u);
            CHECK(nextMusicTrackIndex(MusicTrackMode::Random, album, "", 0, 9) == 0u);
        }
    }

    TEST_CASE("the track mode button cycles Play All, Random, Repeat, Custom", "[music]")
    {
        CHECK(nextStage(MusicTrackMode::PlayAll) == MusicTrackMode::Random);
        CHECK(nextStage(MusicTrackMode::Random) == MusicTrackMode::Repeat);
        CHECK(nextStage(MusicTrackMode::Repeat) == MusicTrackMode::Custom);
        CHECK(nextStage(MusicTrackMode::Custom) == MusicTrackMode::PlayAll);
    }

    TEST_CASE("the track type button cycles Building, Battle, Victory, Defeat, Unused", "[music]")
    {
        REQUIRE(nextStage(MusicTrackType::Building) == MusicTrackType::Battle);
        REQUIRE(nextStage(MusicTrackType::Battle) == MusicTrackType::Victory);
        REQUIRE(nextStage(MusicTrackType::Victory) == MusicTrackType::Defeat);
        REQUIRE(nextStage(MusicTrackType::Defeat) == MusicTrackType::Unused);
        REQUIRE(nextStage(MusicTrackType::Unused) == MusicTrackType::Building);
    }

    TEST_CASE("the per-track types survive a trip through rwe.cfg", "[music]")
    {
        std::vector<unsigned int> types{0u, 1u, 4u, 2u, 3u};
        REQUIRE(formatMusicTrackTypes(types) == "0,1,4,2,3");
        REQUIRE(parseMusicTrackTypes("0,1,4,2,3") == types);
        REQUIRE(parseMusicTrackTypes("").empty());
        // Spaces are tolerated, a stray word reads as Building so the list
        // stays aligned with the album, and a number past Unused is clamped.
        REQUIRE(parseMusicTrackTypes(" 1, x ,9") == std::vector<unsigned int>{1u, 0u, 4u});
    }

    TEST_CASE("a track is typed by the player's list, else by its name", "[music]")
    {
        std::vector<std::string> album{"music/Brutal Battle.mp3", "music/Nine Blossoms.mp3", "music/Attack.mp3"};
        std::vector<unsigned int> types{};

        // No list at all: the name table decides.
        REQUIRE(musicTrackTypeOf(types, 0, album[0]) == MusicTrackType::Battle);
        REQUIRE(musicTrackTypeOf(types, 1, album[1]) == MusicTrackType::Building);

        // A chosen type wins, and a list shorter than the album leaves the
        // rest to their names.
        types = {4u};
        REQUIRE(musicTrackTypeOf(types, 0, album[0]) == MusicTrackType::Unused);
        REQUIRE(musicTrackTypeOf(types, 2, album[2]) == MusicTrackType::Battle);
    }

    TEST_CASE("the album leaves the title theme out and numbers from one", "[music]")
    {
        auto album = buildMusicAlbum({"music/Attack.mp3", "music/Total Annihilation Theme.mp3", "music/Warpath.mp3"});
        REQUIRE(album == std::vector<std::string>{"music/Attack.mp3", "music/Warpath.mp3"});
        REQUIRE(musicTrackNumberCaption(std::size_t(0)) == "1");
        REQUIRE(musicTrackNumberCaption(std::size_t(11)) == "12");
        REQUIRE(musicTrackNumberCaption(std::nullopt) == "NO DISC");
    }

    TEST_CASE("Custom mode plays by type, and Victory, Defeat and Unused sit out", "[music]")
    {
        std::vector<std::string> album{"music/Attack.mp3", "music/Nine Blossoms.mp3", "music/Warpath.mp3", "music/Fire and Ice.mp3"};

        SECTION("by name, a battle track is battle music")
        {
            auto moods = splitMusicMoods(album, {});
            REQUIRE(moods.battle == std::vector<std::string>{"music/Attack.mp3", "music/Warpath.mp3", "music/Fire and Ice.mp3"});
            REQUIRE(moods.building == std::vector<std::string>{"music/Nine Blossoms.mp3"});
        }

        SECTION("retyping moves it, and Unused takes it out of both")
        {
            // Attack -> Building, Warpath -> Unused, Fire and Ice -> Victory.
            auto moods = splitMusicMoods(album, {0u, 0u, 4u, 2u});
            REQUIRE(moods.building == std::vector<std::string>{"music/Attack.mp3", "music/Nine Blossoms.mp3"});
            // Nothing left for battle, so it borrows the building list.
            REQUIRE(moods.battle == moods.building);
        }
    }

    TEST_CASE("the sound mix cannot sum past its budget, at any count", "[audio]")
    {
        // The loudness law and the gain conversion only mean anything read
        // together: computeSoundVolume hands out a 0-128 channel volume per
        // sound and computeEffectGain turns that into a track gain, so what
        // the mixer actually has to fit is the two multiplied by the number
        // of sounds -- the worst case, where every one of them peaks in the
        // same instant. Before the base gain was folded back in that sum
        // reached 8.0 and the mixer could only clamp it, which is what a
        // loud battle sounded like (issue #58).
        constexpr float baseGain = 0.5f;
        for (int soundCount = 1; soundCount <= 256; ++soundCount)
        {
            auto gain = computeEffectGain(computeSoundVolume(soundCount), baseGain, 1.0f, true);
            INFO("soundCount " << soundCount << ", gain " << gain);
            REQUIRE(gain > 0.0f);
            REQUIRE((gain * static_cast<float>(soundCount)) <= 2.0f);
        }
    }

    TEST_CASE("the retune leaves a dense battle exactly where it was", "[audio]")
    {
        // Old curve: base 0.25, ceiling 8. New: base 0.5, ceiling 4. Both
        // come to 2/N once the ceiling is reached, so from sixteen
        // concurrent sounds upward nothing moves at all -- which is what
        // makes the retune safe. The battle that stopped crackling is
        // untouched and only the sparse end comes up to meet it.
        REQUIRE(computeEffectGain(computeSoundVolume(16), 0.5f, 1.0f, true) == 0.125f);
        REQUIRE(computeEffectGain(computeSoundVolume(32), 0.5f, 1.0f, true) == 0.0625f);

        // A lone sound, and a handful, are twice what they were.
        REQUIRE(computeEffectGain(computeSoundVolume(1), 0.5f, 1.0f, true) == 0.5f);
        REQUIRE(computeEffectGain(computeSoundVolume(4), 0.5f, 1.0f, true) == 0.5f);
    }
}
