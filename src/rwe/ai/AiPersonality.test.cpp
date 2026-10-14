#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <rwe/ai/AiPersonality.h>
#include <stdexcept>

namespace rwe
{
    TEST_CASE("every built-in personality sets only knobs the profile has", "[ai]")
    {
        const auto& all = builtInAiPersonalities();
        REQUIRE(!all.empty());
        CHECK(all.front().name == "Original TA");
        for (const auto& personality : all)
        {
            INFO(personality.name);
            AiTuningProfile profile;
            CHECK(applyAiPersonality(profile, personality) == std::nullopt);
        }
    }

    TEST_CASE("Original TA is the classic engine and every other built-in is RWE's", "[ai]")
    {
        const auto& all = builtInAiPersonalities();
        REQUIRE(!all.empty());
        const auto& original = all.front();
        CHECK(original.name == "Original TA");
        CHECK(original.description == "The original Total Annihilation computer player, as the executable plays it.");
        CHECK(original.engine == AiEngine::Classic);
        CHECK(original.difficulty == std::nullopt);
        CHECK(original.knobs.empty());

        for (std::size_t i = 1; i < all.size(); ++i)
        {
            INFO(all[i].name);
            CHECK(all[i].engine == AiEngine::Rwe);
        }
    }

    TEST_CASE("the easy, medium and hard personalities set the difficulty and nothing else", "[ai]")
    {
        const auto& all = builtInAiPersonalities();
        auto easy = findAiPersonality(all, "Easy");
        auto medium = findAiPersonality(all, "medium");
        auto hard = findAiPersonality(all, "HARD");
        REQUIRE(easy);
        REQUIRE(medium);
        REQUIRE(hard);
        CHECK(easy->difficulty == AiDifficulty::Easy);
        CHECK(medium->difficulty == AiDifficulty::Standard);
        CHECK(hard->difficulty == AiDifficulty::Hard);
        CHECK(easy->knobs.empty());
        CHECK(medium->knobs.empty());
        CHECK(hard->knobs.empty());
    }

    TEST_CASE("a personality's knobs land on the profile", "[ai]")
    {
        auto rush = findAiPersonality(builtInAiPersonalities(), "rush");
        REQUIRE(rush);
        AiTuningProfile profile;
        REQUIRE(applyAiPersonality(profile, *rush) == std::nullopt);
        CHECK(profile.attackArmySize == 5);
        CHECK(profile.targetDefenceCount == 0);
        CHECK(!profile.techLevelTwo);

        auto turtle = findAiPersonality(builtInAiPersonalities(), "Turtle");
        REQUIRE(turtle);
        AiTuningProfile turtleProfile;
        REQUIRE(applyAiPersonality(turtleProfile, *turtle) == std::nullopt);
        CHECK(turtleProfile.targetDefenceCount == 6);
        CHECK(turtleProfile.fortifyTowers);
        CHECK(turtleProfile.raidingParties == 0);
    }

    TEST_CASE("a personality file is read line by line", "[ai]")
    {
        SECTION("name, description, difficulty and knobs")
        {
            auto p = parseAiPersonality(
                "# A comment.\n"
                "; another\n"
                "\n"
                "name = Raider\r\n"
                "description=Raids from the first minute.\n"
                "difficulty=hard\n"
                "attackArmySize=4\n"
                "targetDefenceCount = 0\n",
                "fallback");
            CHECK(p.name == "Raider");
            CHECK(p.description == "Raids from the first minute.");
            CHECK(p.difficulty == AiDifficulty::Hard);
            REQUIRE(p.knobs.size() == 2);
            CHECK(p.knobs[0] == std::make_pair(std::string("attackArmySize"), std::string("4")));
            CHECK(p.knobs[1] == std::make_pair(std::string("targetDefenceCount"), std::string("0")));

            AiTuningProfile profile;
            REQUIRE(applyAiPersonality(profile, p) == std::nullopt);
            CHECK(profile.attackArmySize == 4);
            CHECK(profile.targetDefenceCount == 0);
        }

        SECTION("the file's name stands in when it names nothing")
        {
            auto p = parseAiPersonality("attackArmySize=9\n", "Nine");
            CHECK(p.name == "Nine");
            CHECK(p.difficulty == std::nullopt);
        }

        SECTION("a misspelt knob is refused")
        {
            CHECK_THROWS_AS(parseAiPersonality("atackArmySize=4\n", "x"), std::runtime_error);
        }

        SECTION("a value the knob cannot take is refused")
        {
            CHECK_THROWS_AS(parseAiPersonality("attackArmySize=lots\n", "x"), std::runtime_error);
        }

        SECTION("a line that is not key=value is refused")
        {
            CHECK_THROWS_AS(parseAiPersonality("attackArmySize 4\n", "x"), std::runtime_error);
        }

        SECTION("an unknown difficulty is refused")
        {
            CHECK_THROWS_AS(parseAiPersonality("difficulty=impossible\n", "x"), std::runtime_error);
        }
    }

    TEST_CASE("a personality file names its engine, and is RWE's when it does not", "[ai]")
    {
        SECTION("no engine line is RWE's, as every file written before the choice was")
        {
            auto p = parseAiPersonality("attackArmySize=4\n", "old");
            CHECK(p.engine == AiEngine::Rwe);
        }

        SECTION("engine=rwe")
        {
            auto p = parseAiPersonality("engine=rwe\nattackArmySize=4\n", "x");
            CHECK(p.engine == AiEngine::Rwe);
            CHECK(p.knobs.size() == 1);
        }

        SECTION("engine=classic, in any case, with a difficulty and no knobs")
        {
            auto p = parseAiPersonality("name=Plain\nEngine = Classic\ndifficulty=hard\n", "x");
            CHECK(p.engine == AiEngine::Classic);
            CHECK(p.difficulty == AiDifficulty::Hard);
            CHECK(p.knobs.empty());
        }

        SECTION("an engine it does not know is refused, naming the line")
        {
            try
            {
                parseAiPersonality("name=X\nengine=quantum\n", "x");
                FAIL("an unknown engine was accepted");
            }
            catch (const std::runtime_error& e)
            {
                CHECK(std::string(e.what()).find("line 2") != std::string::npos);
                CHECK(std::string(e.what()).find("quantum") != std::string::npos);
            }
        }

        SECTION("a classic personality that lists a knob is refused, wherever the engine line is")
        {
            for (const char* text : {"engine=classic\nattackArmySize=4\n", "attackArmySize=4\nengine=classic\n"})
            {
                INFO(text);
                try
                {
                    parseAiPersonality(text, "x");
                    FAIL("a knob on a classic personality was accepted");
                }
                catch (const std::runtime_error& e)
                {
                    CHECK(std::string(e.what()).find("the classic engine has no knobs") != std::string::npos);
                }
            }
        }
    }

    TEST_CASE("personality files join the built-ins, and one of the same name replaces it", "[ai]")
    {
        auto dir = std::filesystem::temp_directory_path() / "rwe-ai-personality-test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        {
            std::ofstream(dir / "raider.txt") << "description=Mine.\nattackArmySize=3\n";
            std::ofstream(dir / "rush.TXT") << "name=Rush\nattackArmySize=7\n";
            std::ofstream(dir / "broken.txt") << "noSuchKnob=1\n";
            std::ofstream(dir / "notes.md") << "attackArmySize=1\n";
        }

        auto all = loadAiPersonalities(dir);
        std::filesystem::remove_all(dir);

        auto raider = findAiPersonality(all, "raider");
        REQUIRE(raider);
        CHECK(raider->description == "Mine.");

        auto rush = findAiPersonality(all, "Rush");
        REQUIRE(rush);
        REQUIRE(rush->knobs.size() == 1);
        CHECK(rush->knobs[0].second == "7");

        CHECK(!findAiPersonality(all, "broken"));
        CHECK(!findAiPersonality(all, "notes"));
        CHECK(all.size() == builtInAiPersonalities().size() + 1);
    }

    TEST_CASE("no personality directory leaves the built-ins", "[ai]")
    {
        CHECK(loadAiPersonalities(std::nullopt).size() == builtInAiPersonalities().size());
        CHECK(loadAiPersonalities(std::filesystem::path("Z:/no/such/place")).size() == builtInAiPersonalities().size());
    }
}
