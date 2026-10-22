#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <rwe/ai/AiPersonality.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/game/AiSeat.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <stdexcept>

/**
 * The seam between a seat and the engine that plays it (ADR-0005): which
 * engine a seat gets, what the classic engine is built from, and the one
 * thing both engines owe the simulation, a single draw from its generator.
 */
namespace rwe
{
    namespace
    {
        std::string lowered(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        /** Files by name, found case-insensitively like the real one. */
        class FakeVfs final : public AbstractVirtualFileSystem
        {
        public:
            std::map<std::string, std::string> files;

            std::optional<std::vector<char>> readFile(const std::string& filename) const override
            {
                auto it = files.find(lowered(filename));
                if (it == files.end())
                {
                    return std::nullopt;
                }
                return std::vector<char>(it->second.begin(), it->second.end());
            }

            std::vector<std::string> getFileNames(const std::string&, const std::string&) override { return {}; }
            std::vector<std::string> getDirectoryNames(const std::string&) override { return {}; }
            std::vector<std::string> getFileNamesRecursive(const std::string&, const std::string&) override { return {}; }
        };

        GameSimulation makeSimulation()
        {
            GameSimulation sim(makeFlatTerrain(32, 32), 0u, 100, 3000);
            sim.rng.seed(1234u);
            return sim;
        }

        AiSeatRequest seat(unsigned int player = 0)
        {
            AiSeatRequest request;
            request.playerId = PlayerId(player);
            request.side = "ARM";
            return request;
        }

        std::unique_ptr<AiController> build(GameSimulation& sim, const AiSeatRequest& request)
        {
            return createAiController(sim, request, MapIntel{}, AiBuildTree{});
        }

        std::optional<AiPersonality> builtIn(const std::string& name)
        {
            return findAiPersonality(builtInAiPersonalities(), name);
        }
    }

    TEST_CASE("RWE mission seats apply mission profile limits and difficulty weights", "[ai][engine][mission-profile]")
    {
        // rev31/ai/MISSIONS.TXT, SHA256 a48c3791...dd2619d.
        // Same directives as the shipped file, with comments/blank lines omitted.
        FakeVfs vfs;
        vfs.files["ai/missions.txt"] =
            "plan easy\nlimit constr 1\nweight level2 0.25\n"
            "limit corint 0\nlimit armbrtha 0\nlimit armanni 0\nlimit cordoom 0\nlimit cortron 0\nlimit armemp 0\n"
            "plan medium\nweight level2 0.5\nlimit constr 2\n"
            "limit corint 0\nlimit armbrtha 0\nlimit armanni 0\nlimit cordoom 0\nlimit cortron 0\nlimit armemp 0\n"
            "plan hard\nweight level2 2\nlimit constr 8\nlimit cortron 0\nlimit armemp 0\n";
        for (const auto difficulty : {AiDifficulty::Easy, AiDifficulty::Standard, AiDifficulty::Hard})
        {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            sim.unitDefinitions["ARMCK"].category = "CONSTR LEVEL1";
            sim.unitDefinitions["ARMZEUS"].category = "LEVEL2";
            sim.unitDefinitions["ARMBRTHA"].category = "LEVEL2";
            sim.unitDefinitions["CORTRON"].category = "LEVEL2";
            AiSeatRequest request;
            request.defaultEngine = AiEngine::Rwe;
            request.gameDifficulty = difficulty;
            request.vfs = &vfs;
            request.aiProfileName = "MISSIONS";
            request.mission = true;
            auto controller = createAiController(sim, request, MapIntel{}, AiBuildTree{});
            const auto& tree = controller->asRweController()->getBlackboard().buildTree;
            CHECK(tree.missionRules.at("ARMCK").limit == (difficulty == AiDifficulty::Easy ? 1 : difficulty == AiDifficulty::Standard ? 2 : 8));
            CHECK(tree.missionWeight("ARMZEUS") == (difficulty == AiDifficulty::Easy ? 25 : difficulty == AiDifficulty::Standard ? 50 : 100));
            CHECK(tree.missionAllows("ARMBRTHA") == (difficulty == AiDifficulty::Hard));
            CHECK_FALSE(tree.missionAllows("CORTRON"));
            request.mission = false;
            auto skirmish = createAiController(sim, request, MapIntel{}, AiBuildTree{});
            CHECK(skirmish->asRweController()->getBlackboard().buildTree.missionRules.empty());
        }
    }
    TEST_CASE("the engine a seat plays is its personality's, then its saved one, then the game's", "[ai][engine]")
    {
        auto balanced = builtIn("Balanced");
        auto original = builtIn("Original TA");
        REQUIRE(balanced);
        REQUIRE(original);

        SECTION("a seat with nothing said plays the game's default")
        {
            CHECK(chooseAiEngine(std::nullopt, std::nullopt, AiEngine::Classic) == AiEngine::Classic);
            CHECK(chooseAiEngine(std::nullopt, std::nullopt, AiEngine::Rwe) == AiEngine::Rwe);
        }

        SECTION("a saved engine beats the default")
        {
            CHECK(chooseAiEngine(std::nullopt, AiEngine::Rwe, AiEngine::Classic) == AiEngine::Rwe);
            CHECK(chooseAiEngine(std::nullopt, AiEngine::Classic, AiEngine::Rwe) == AiEngine::Classic);
        }

        SECTION("a personality that was found beats both")
        {
            CHECK(chooseAiEngine(balanced, AiEngine::Classic, AiEngine::Classic) == AiEngine::Rwe);
            CHECK(chooseAiEngine(original, AiEngine::Rwe, AiEngine::Rwe) == AiEngine::Classic);
        }
    }

    TEST_CASE("a seat with no personality is built as the classic engine", "[ai][engine]")
    {
        auto sim = makeSimulation();
        auto controller = build(sim, seat(3));
        REQUIRE(controller);
        CHECK(controller->engine() == AiEngine::Classic);
        CHECK(controller->getPlayerId() == PlayerId(3u));
        CHECK(controller->asRweController() == nullptr);
    }

    TEST_CASE("Balanced and the other built-ins are built as RWE's engine", "[ai][engine]")
    {
        for (const char* name : {"Balanced", "Rush", "Turtle", "Tech Rush", "Easy", "Medium", "Hard"})
        {
            INFO(name);
            auto sim = makeSimulation();
            auto request = seat();
            request.personality = builtIn(name);
            auto controller = build(sim, request);
            REQUIRE(controller);
            CHECK(controller->engine() == AiEngine::Rwe);
            CHECK(controller->asRweController() != nullptr);
        }
    }

    TEST_CASE("Original TA is the classic engine and carries no difficulty of its own", "[ai][engine]")
    {
        auto original = builtIn("original ta");
        REQUIRE(original);
        CHECK(original->engine == AiEngine::Classic);
        CHECK(original->difficulty == std::nullopt);
        CHECK(original->knobs.empty());

        auto sim = makeSimulation();
        auto request = seat();
        request.personality = original;
        request.gameDifficulty = AiDifficulty::Hard;
        auto controller = build(sim, request);
        CHECK(controller->engine() == AiEngine::Classic);
        // The game's difficulty applies to it, because the personality names none.
        CHECK(controller->getProfile().difficulty == AiDifficulty::Hard);
    }

    TEST_CASE("the engine a saved game recorded is the one that is built", "[ai][engine]")
    {
        SECTION("RWE's, against the classic default")
        {
            auto sim = makeSimulation();
            auto request = seat();
            request.savedEngine = AiEngine::Rwe;
            request.defaultEngine = AiEngine::Classic;
            CHECK(build(sim, request)->engine() == AiEngine::Rwe);
        }

        SECTION("the classic one, against an RWE default")
        {
            auto sim = makeSimulation();
            auto request = seat();
            request.savedEngine = AiEngine::Classic;
            request.defaultEngine = AiEngine::Rwe;
            CHECK(build(sim, request)->engine() == AiEngine::Classic);
        }

        SECTION("a personality the save names is still the personality's")
        {
            auto sim = makeSimulation();
            auto request = seat();
            request.savedEngine = AiEngine::Classic;
            request.personality = builtIn("Turtle");
            CHECK(build(sim, request)->engine() == AiEngine::Rwe);
        }
    }

    TEST_CASE("--ai-tune aimed at a classic seat is an error that says what to do", "[ai][engine]")
    {
        auto sim = makeSimulation();
        auto request = seat(1);
        request.tuning = {"1:attackArmySize=9"};

        try
        {
            build(sim, request);
            FAIL("a knob on a classic seat was accepted");
        }
        catch (const std::runtime_error& e)
        {
            CHECK(std::string(e.what()) == "--ai-tune: player 1 plays the classic engine, which has no knobs (use --ai-engine rwe)");
        }

        SECTION("a knob for somebody else is not its business")
        {
            request.tuning = {"0:attackArmySize=9"};
            CHECK_NOTHROW(build(sim, request));
        }

        SECTION("the same entry on an RWE seat lands on its profile")
        {
            request.defaultEngine = AiEngine::Rwe;
            auto controller = build(sim, request);
            REQUIRE(controller->asRweController() != nullptr);
            CHECK(controller->getProfile().attackArmySize == 9);
        }

        SECTION("a misspelt knob on an RWE seat is still fatal")
        {
            request.defaultEngine = AiEngine::Rwe;
            request.tuning = {"1:noSuchKnob=9"};
            CHECK_THROWS_AS(build(sim, request), std::runtime_error);
        }
    }

    TEST_CASE("each seat draws exactly one value from the simulation's generator, whichever engine plays it", "[ai][engine]")
    {
        // The reference: one draw, taken by hand.
        auto reference = makeSimulation();
        (void)reference.rng();
        const auto afterOne = reference.rng;

        for (const bool idle : {false, true})
        {
            for (const auto engine : {AiEngine::Classic, AiEngine::Rwe})
            {
                INFO("engine " << aiEngineName(engine) << ", idle " << idle);
                auto sim = makeSimulation();
                auto request = seat();
                request.defaultEngine = engine;
                request.idle = idle;
                auto controller = build(sim, request);
                REQUIRE(controller);
                CHECK(controller->engine() == engine);
                CHECK(sim.rng == afterOne);
            }
        }
    }

    TEST_CASE("an idle seat is still constructed", "[ai][engine]")
    {
        auto sim = makeSimulation();
        auto request = seat();
        request.defaultEngine = AiEngine::Rwe;
        request.idle = true;
        auto controller = build(sim, request);
        REQUIRE(controller);
        CHECK(controller->getProfile().idle);

        std::vector<PlayerCommand> out;
        controller->tick(sim, out);
        CHECK(out.empty());
    }

    TEST_CASE("the classic engine reads its profile from the mission's aiprofile, then the default", "[ai][engine]")
    {
        FakeVfs vfs;
        vfs.files["ai/default.txt"] = "default profile";

        SECTION("the named file wins, found whatever its case")
        {
            vfs.files["ai/radai.txt"] = "the mission's profile";
            auto file = readClassicAiProfile(vfs, "RADAI");
            CHECK(file.text == "the mission's profile");
            CHECK(file.path == "ai/RADAI.txt");
        }

        SECTION("without it, ai/default.txt")
        {
            auto file = readClassicAiProfile(vfs, "RADAI");
            CHECK(file.text == "default profile");
            CHECK(file.path == "ai/default.txt");
        }

        SECTION("the skirmish map's DEFAULT is the default file")
        {
            auto file = readClassicAiProfile(vfs, "DEFAULT");
            CHECK(file.text == "default profile");
        }

        SECTION("with neither, nothing")
        {
            vfs.files.clear();
            auto file = readClassicAiProfile(vfs, "RADAI");
            CHECK(file.text.empty());
            CHECK(file.path.empty());
        }

        SECTION("an empty name falls straight to the default")
        {
            auto file = readClassicAiProfile(vfs, "");
            CHECK(file.text == "default profile");
        }
    }

    TEST_CASE("a classic seat is built with a file system, a missing profile and all", "[ai][engine]")
    {
        FakeVfs empty;
        auto sim = makeSimulation();
        auto request = seat();
        request.vfs = &empty;
        request.aiProfileName = "NOPE";
        auto controller = build(sim, request);
        REQUIRE(controller);
        CHECK(controller->engine() == AiEngine::Classic);
    }
}
