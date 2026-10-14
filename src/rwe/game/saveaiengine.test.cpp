#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/classic/ClassicAiController.h>
#include <rwe/game/SaveFile.h>
#include <rwe/game/save_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/sim_test_util.h>

/**
 * Which computer player plays each seat survives a save (ADR-0005), and a save
 * from before there was a choice comes back as RWE's own rather than as the new
 * default, so that a game in progress does not change opponent when it is
 * loaded.
 */
namespace rwe
{
    namespace
    {
        namespace fs = std::filesystem;

        struct TempSave
        {
            fs::path path;

            TempSave()
            {
                auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                path = fs::temp_directory_path() / ("rwe-saveaiengine-test-" + std::to_string(stamp) + ".rwesave");
            }

            ~TempSave()
            {
                std::error_code ec;
                fs::remove(path, ec);
            }
        };

        PlayerInfo makeSeat(PlayerControllerType controller)
        {
            return PlayerInfo{
                std::nullopt,
                controller,
                "ARM",
                PlayerColorIndex(0),
                Metal(1000.0f),
                Energy(1000.0f)};
        }

        SaveFile makeSave()
        {
            SaveFile save(GameParameters("SomeMap", 0));
            save.simulation = nlohmann::json::object();
            return save;
        }

        /** Rewrites the file's header as plain json, the way an older build would have written it. */
        void editHeader(const fs::path& path, const std::function<void(nlohmann::json&)>& edit)
        {
            std::ifstream in(path, std::ios::binary);
            auto j = nlohmann::json::parse(in);
            in.close();
            edit(j.at("header"));
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << j.dump();
        }

        PlayerId addComputer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("ai"),
                GamePlayerType::Computer,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }
    }

    TEST_CASE("a computer player's engine round-trips through the save header", "[saveload][savefile][ai]")
    {
        TempSave file;
        auto save = makeSave();
        save.parameters.players[0] = makeSeat(PlayerControllerTypeComputer());
        save.parameters.players[0]->aiEngine = AiEngine::Classic;
        save.parameters.players[1] = makeSeat(PlayerControllerTypeComputer());
        save.parameters.players[1]->aiEngine = AiEngine::Rwe;
        save.parameters.players[1]->aiPersonality = "Turtle";

        writeSaveFile(file.path, save);

        SECTION("spelt as the command line spells it")
        {
            std::ifstream in(file.path, std::ios::binary);
            auto j = nlohmann::json::parse(in);
            const auto& players = j.at("header").at("players");
            CHECK(players.at(0).at("aiEngine") == "classic");
            CHECK(players.at(1).at("aiEngine") == "rwe");
        }

        SECTION("and read back")
        {
            auto loaded = readSaveFile(file.path);
            REQUIRE(loaded.has_value());
            REQUIRE(loaded->parameters.players[0].has_value());
            REQUIRE(loaded->parameters.players[1].has_value());
            CHECK(loaded->parameters.players[0]->aiEngine == AiEngine::Classic);
            CHECK(loaded->parameters.players[1]->aiEngine == AiEngine::Rwe);
            CHECK(loaded->parameters.players[1]->aiPersonality == std::optional<std::string>("Turtle"));
        }
    }

    TEST_CASE("a save from before the engine was recorded loads its computer players as RWE's", "[saveload][savefile][ai]")
    {
        TempSave file;
        auto save = makeSave();
        save.parameters.players[0] = makeSeat(PlayerControllerTypeComputer());
        save.parameters.players[0]->aiEngine = AiEngine::Classic;
        save.parameters.players[1] = makeSeat(PlayerControllerTypeHuman());
        writeSaveFile(file.path, save);

        // What a build without the key would have written.
        editHeader(file.path, [](nlohmann::json& header) { header.at("players").at(0).erase("aiEngine"); });

        auto loaded = readSaveFile(file.path);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->parameters.players[0].has_value());
        CHECK(loaded->parameters.players[0]->aiEngine == AiEngine::Rwe);

        // A person has no engine to be played by.
        REQUIRE(loaded->parameters.players[1].has_value());
        CHECK(loaded->parameters.players[1]->aiEngine == std::nullopt);
    }

    TEST_CASE("an engine name this build does not know loads as RWE's, not as an unreadable save", "[saveload][savefile][ai]")
    {
        TempSave file;
        auto save = makeSave();
        save.parameters.players[0] = makeSeat(PlayerControllerTypeComputer());
        writeSaveFile(file.path, save);
        editHeader(file.path, [](nlohmann::json& header) { header.at("players").at(0)["aiEngine"] = "quantum"; });

        auto loaded = readSaveFile(file.path);
        REQUIRE(loaded.has_value());
        CHECK(loaded->parameters.players[0]->aiEngine == AiEngine::Rwe);
    }

    TEST_CASE("recordAiEngines takes each seat's engine from its live controller", "[saveload][ai]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 100, 3000);
        // Slots 0, 2 and 3 are filled; the simulation numbers them 0, 1 and 2.
        auto classic = addComputer(sim);
        auto rwe = addComputer(sim);
        auto human = addPlayer(sim);
        sim.addAiController(classic, std::make_unique<ClassicAiController>(classic, ClassicAiConfig{}, 1u));
        sim.addAiController(rwe, std::make_unique<AiPlayerController>(rwe, makeDefaultStandardProfile(), 1u, MapIntel{}));

        GameParameters parameters("SomeMap", 0);
        parameters.players[0] = makeSeat(PlayerControllerTypeComputer());
        parameters.players[2] = makeSeat(PlayerControllerTypeComputer());
        parameters.players[3] = makeSeat(PlayerControllerTypeHuman());
        (void)human;

        recordAiEngines(parameters, sim);

        CHECK(parameters.players[0]->aiEngine == AiEngine::Classic);
        CHECK(parameters.players[2]->aiEngine == AiEngine::Rwe);
        CHECK(parameters.players[3]->aiEngine == std::nullopt);
        CHECK_FALSE(parameters.players[1].has_value());
    }
}
