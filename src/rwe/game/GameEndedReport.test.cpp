#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <rwe/game/GameEndedReport.h>

namespace rwe
{
    namespace
    {
        std::filesystem::path writeTempFile(const std::string& name)
        {
            auto path = std::filesystem::temp_directory_path() / name;
            std::ofstream out(path, std::ios::binary);
            out << "x";
            return path;
        }
    }

    TEST_CASE("gameEndedJson")
    {
        SECTION("a decided game names its winner and the tick")
        {
            GameEndedReport report;
            report.outcome = "decided";
            report.winner = PlayerId(1);
            report.tick = SceneTime(4281);
            report.gameTimeSeconds = 142;
            report.engineBuild = "Robot War Engine v1.1.0-pre2-Debug";

            auto json = gameEndedJson(report);

            REQUIRE(json["event"] == "game-ended");
            REQUIRE(json["outcome"] == "decided");
            REQUIRE(json["winner"] == 1);
            REQUIRE(json["tick"] == 4281);
            REQUIRE(json["gameTime"] == 142);
            REQUIRE(json["engineBuild"] == "Robot War Engine v1.1.0-pre2-Debug");
            REQUIRE(!json.contains("winners"));
            REQUIRE(!json.contains("desyncTick"));
        }

        SECTION("a draw carries no winner")
        {
            GameEndedReport report;
            report.outcome = "draw";
            report.tick = SceneTime(100);

            auto json = gameEndedJson(report);

            REQUIRE(json["outcome"] == "draw");
            REQUIRE(json["winners"] == nlohmann::json::array());
            REQUIRE(!json.contains("winner"));
        }

        SECTION("an abandoned game says so and names no winner")
        {
            GameEndedReport report;
            report.outcome = "abandoned";
            report.tick = SceneTime(55);

            auto json = gameEndedJson(report);

            REQUIRE(json["outcome"] == "abandoned");
            REQUIRE(!json.contains("winner"));
            REQUIRE(!json.contains("winners"));
        }

        SECTION("a desync names the tick it was detected at")
        {
            GameEndedReport report;
            report.outcome = "abandoned";
            report.tick = SceneTime(4305);
            report.desyncTick = SceneTime(4281);

            auto json = gameEndedJson(report);

            REQUIRE(json["desyncTick"] == 4281);
        }

        SECTION("a path that exists is sent and a path that does not is null")
        {
            auto present = writeTempFile("rwe-game-ended-present.txt");

            GameEndedReport report;
            report.outcome = "decided";
            report.replayPath = present;
            report.hashLogPath = std::filesystem::temp_directory_path() / "rwe-game-ended-absent-hash.log";
            report.logPath = std::filesystem::path();

            auto json = gameEndedJson(report);

            REQUIRE(json["replay"] == present.string());
            REQUIRE(json["hashLog"].is_null());
            REQUIRE(json["log"].is_null());

            std::error_code error;
            std::filesystem::remove(present, error);
        }

        SECTION("every desync dump that is on disk is listed")
        {
            auto first = writeTempFile("rwe-desync-tick4281-player0.json");
            auto missing = std::filesystem::temp_directory_path() / "rwe-desync-tick4281-player1.json";

            GameEndedReport report;
            report.outcome = "abandoned";
            report.desyncDumpPaths = {first, missing};

            auto json = gameEndedJson(report);

            REQUIRE(json["desyncDumps"].size() == 1);
            REQUIRE(json["desyncDumps"][0] == first.string());

            std::error_code error;
            std::filesystem::remove(first, error);
        }
    }
}
