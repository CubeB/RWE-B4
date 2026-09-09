#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <rwe/game/SaveFile.h>

/**
 * The save-game header's own round trip: not the whole save (that is
 * saveload.test.cpp, hash-validated against a running simulation), just the
 * lobby fields SaveFile.cpp reads and writes on its own -- a player's
 * teamId, and the elapsed game time the save list's TIME column reads.
 *
 * Both are optional fields added after the header format was already
 * shipping, so each is tested for the gap it exists to close: teamId used
 * to be dropped by the header entirely (CLAUDE.md's "Saved games" section
 * used to record that as a known gap), and gameTimeSeconds has to stay
 * readable on a save that predates it.
 */
namespace rwe
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path uniqueTempPath()
        {
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            return fs::temp_directory_path() / ("rwe-savefileheader-test-" + std::to_string(stamp) + ".rwesave");
        }

        /** Deletes its file however the test leaves, passing or failing. */
        struct TempFile
        {
            fs::path path{uniqueTempPath()};

            ~TempFile()
            {
                std::error_code ec;
                fs::remove(path, ec);
            }
        };

        PlayerInfo makePlayer(std::optional<int> teamId)
        {
            PlayerInfo p{
                std::string("Commander"),
                PlayerControllerTypeHuman(),
                "ARM",
                PlayerColorIndex(0),
                Metal(1000.0f),
                Energy(1000.0f),
                teamId};
            return p;
        }

        SaveFile makeSaveFile()
        {
            GameParameters parameters("SomeMap", 0);
            SaveFile save(parameters);
            save.simulation = nlohmann::json::object();
            return save;
        }
    }

    TEST_CASE("SaveFile's header round-trips a player's teamId", "[saveload][savefile]")
    {
        SECTION("a player with a team")
        {
            TempFile file;
            auto save = makeSaveFile();
            save.parameters.players[0] = makePlayer(std::optional<int>(3));

            writeSaveFile(file.path, save);
            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE(loaded->parameters.players[0].has_value());
            REQUIRE(loaded->parameters.players[0]->teamId == std::optional<int>(3));
        }

        SECTION("a player with no team")
        {
            TempFile file;
            auto save = makeSaveFile();
            save.parameters.players[0] = makePlayer(std::nullopt);

            writeSaveFile(file.path, save);
            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE(loaded->parameters.players[0].has_value());
            REQUIRE(!loaded->parameters.players[0]->teamId.has_value());
        }
    }

    TEST_CASE("SaveFile's header round-trips gameTimeSeconds", "[saveload][savefile]")
    {
        SECTION("a save written with a game time reads it back")
        {
            TempFile file;
            auto save = makeSaveFile();
            save.gameTimeSeconds = 2537u;

            writeSaveFile(file.path, save);
            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE(loaded->gameTimeSeconds == std::optional<unsigned int>(2537u));
        }

        SECTION("a save written before the field existed loads with it empty")
        {
            // Built by writing a real save and then taking the key back out,
            // rather than a hand-written document, so this fails the moment
            // the reader starts expecting a key that older saves do not have.
            TempFile file;
            auto save = makeSaveFile();
            save.gameTimeSeconds = 99u;
            writeSaveFile(file.path, save);

            std::ifstream in(file.path, std::ios::binary);
            auto j = nlohmann::json::parse(in);
            in.close();
            j.at("header").erase("gameTimeSeconds");
            std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
            out << j.dump();
            out.close();

            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE(!loaded->gameTimeSeconds.has_value());
        }
    }
}
