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

    TEST_CASE("SaveFile's header round-trips a campaign's progress", "[saveload][savefile][campaign]")
    {
        CampaignProgress progress;
        progress.campaign = "Core Campaign";
        progress.missionIndex = 3;
        progress.difficulty = 2;
        progress.side = 1;
        progress.recordResult(false);
        progress.missionIndex = 4;
        progress.recordResult(true);
        progress.glamour = "cor05";
        progress.glamourSound = "cor05g";
        progress.hasNextMission = true;

        SECTION("a mission's result is its letter in the run")
        {
            REQUIRE(progress.thumbs.size() == 25u);
            REQUIRE(progress.thumbs.substr(0, 6) == "UUULWU");
        }

        SECTION("everything the screens after the game read comes back")
        {
            TempFile file;
            auto save = makeSaveFile();
            save.parameters.mission = true;
            save.parameters.campaign = progress;

            writeSaveFile(file.path, save);
            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE(loaded->parameters.mission);
            REQUIRE(loaded->parameters.campaign.has_value());
            const auto& c = *loaded->parameters.campaign;
            REQUIRE(c.campaign == "Core Campaign");
            REQUIRE(c.missionIndex == 4u);
            REQUIRE(c.difficulty == 2u);
            REQUIRE(c.side == 1u);
            REQUIRE(c.thumbs == progress.thumbs);
            REQUIRE(c.glamour == "cor05");
            REQUIRE(c.glamourSound == "cor05g");
            REQUIRE_FALSE(c.noMovie);
            REQUIRE(c.hasNextMission);
        }

        SECTION("a skirmish save has none")
        {
            TempFile file;
            auto save = makeSaveFile();

            writeSaveFile(file.path, save);
            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE_FALSE(loaded->parameters.mission);
            REQUIRE_FALSE(loaded->parameters.campaign.has_value());
        }

        SECTION("a save between missions says so, and keeps no world")
        {
            TempFile file;
            auto save = makeSaveFile();
            save.parameters.campaign = progress;
            save.betweenMissions = true;
            save.simulation = nlohmann::json();

            writeSaveFile(file.path, save);
            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE(loaded->betweenMissions);
            REQUIRE(loaded->simulation.is_null());
            REQUIRE(loaded->parameters.campaign->missionIndex == 4u);
        }

        SECTION("a save that is not a campaign's is never between missions")
        {
            TempFile file;
            auto save = makeSaveFile();
            save.betweenMissions = true;

            writeSaveFile(file.path, save);
            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE_FALSE(loaded->betweenMissions);
        }

        SECTION("a save written before the campaign keys existed loads as no campaign")
        {
            // Written with them and then taken back out, as the
            // gameTimeSeconds case above does, so this fails the moment the
            // reader starts expecting either key.
            TempFile file;
            auto save = makeSaveFile();
            save.parameters.mission = true;
            save.parameters.campaign = progress;
            writeSaveFile(file.path, save);

            std::ifstream in(file.path, std::ios::binary);
            auto j = nlohmann::json::parse(in);
            in.close();
            j.at("header").erase("campaign");
            j.at("header").erase("mission");
            std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
            out << j.dump();
            out.close();

            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE_FALSE(loaded->parameters.mission);
            REQUIRE_FALSE(loaded->parameters.campaign.has_value());
        }

        SECTION("a run of the wrong length starts again, as the original's loader does")
        {
            TempFile file;
            auto save = makeSaveFile();
            progress.thumbs = "WWL";
            save.parameters.campaign = progress;

            writeSaveFile(file.path, save);
            auto loaded = readSaveFile(file.path);

            REQUIRE(loaded.has_value());
            REQUIRE(loaded->parameters.campaign->thumbs == std::string(25, 'U'));
        }
    }

    TEST_CASE("a save whose header holds a field of the wrong type reads as unreadable", "[saveload][savefile]")
    {
        // The file is anyone's: a hand-edited or corrupt header must fail to
        // read, not throw out of the Load list while a game is running.
        TempFile file;
        writeSaveFile(file.path, makeSaveFile());

        std::ifstream in(file.path, std::ios::binary);
        auto j = nlohmann::json::parse(in);
        in.close();
        j.at("header")["mission"] = "yes";
        std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
        out << j.dump();
        out.close();

        REQUIRE_FALSE(readSaveFile(file.path).has_value());
    }
}
