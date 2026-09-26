#include "GameEndedReport.h"

#include <nlohmann/json.hpp>
#include <system_error>

namespace rwe
{
    namespace
    {
        bool pathExists(const std::optional<std::filesystem::path>& path)
        {
            if (!path)
            {
                return false;
            }

            std::error_code error;
            return std::filesystem::exists(*path, error) && !error;
        }

        // path::string() is the ANSI code page on Windows, and json::dump()
        // throws on anything that is not UTF-8.
        std::string utf8(const std::filesystem::path& path)
        {
            auto u8 = path.u8string();
            return std::string(u8.begin(), u8.end());
        }

        nlohmann::json pathJson(const std::optional<std::filesystem::path>& path)
        {
            if (!pathExists(path))
            {
                return nullptr;
            }
            return utf8(*path);
        }
    }

    nlohmann::json gameEndedJson(const GameEndedReport& report)
    {
        nlohmann::json json{
            {"event", "game-ended"},
            {"outcome", report.outcome},
            {"tick", report.tick.value},
            {"gameTime", report.gameTimeSeconds},
            {"engineBuild", report.engineBuild},
            {"replay", pathJson(report.replayPath)},
            {"hashLog", pathJson(report.hashLogPath)},
            {"desyncDumps", nlohmann::json::array()},
            {"log", pathJson(report.logPath)},
        };

        if (report.engineBuild.empty())
        {
            json.erase("engineBuild");
        }

        if (report.desyncTick)
        {
            json["desyncTick"] = report.desyncTick->value;
        }

        if (report.outcome == "decided")
        {
            json["winner"] = report.winner ? nlohmann::json(report.winner->value) : nlohmann::json(nullptr);
        }
        else if (report.outcome == "draw")
        {
            json["winners"] = nlohmann::json::array();
        }

        for (const auto& path : report.desyncDumpPaths)
        {
            if (pathExists(path))
            {
                json["desyncDumps"].push_back(utf8(path));
            }
        }

        return json;
    }
}
