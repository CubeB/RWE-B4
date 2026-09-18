#include "SaveFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <rwe/util.h>
#include <rwe/util/match.h>
#include <utility>

namespace rwe
{
    namespace
    {
        namespace fs = std::filesystem;

        nlohmann::json playerToJson(const PlayerInfo& p)
        {
            nlohmann::json j;
            if (p.name)
            {
                j["name"] = *p.name;
            }
            j["controller"] = match(
                p.controller,
                [](const PlayerControllerTypeHuman&) { return "human"; },
                [](const PlayerControllerTypeComputer&) { return "computer"; },
                [](const PlayerControllerTypeNetwork&) { return "network"; });
            j["side"] = p.side;
            j["color"] = p.color.value;
            j["metal"] = p.metal.value;
            j["energy"] = p.energy.value;
            if (p.teamId)
            {
                j["teamId"] = *p.teamId;
            }
            return j;
        }

        PlayerInfo playerFromJson(const nlohmann::json& j)
        {
            PlayerInfo p{
                std::nullopt,
                PlayerControllerTypeHuman(),
                j.at("side").get<std::string>(),
                PlayerColorIndex(j.at("color").get<unsigned int>()),
                Metal(j.at("metal").get<float>()),
                Energy(j.at("energy").get<float>())};
            if (j.contains("name"))
            {
                p.name = j.at("name").get<std::string>();
            }
            auto controller = j.at("controller").get<std::string>();
            if (controller == "computer")
            {
                p.controller = PlayerControllerTypeComputer();
            }
            if (j.contains("teamId"))
            {
                p.teamId = j.at("teamId").get<int>();
            }
            return p;
        }

        const char* aiDifficultyToString(AiDifficulty d)
        {
            switch (d)
            {
                case AiDifficulty::Idle:
                    return "idle";
                case AiDifficulty::Easy:
                    return "easy";
                case AiDifficulty::Hard:
                    return "hard";
                case AiDifficulty::Brutal:
                    return "brutal";
                default:
                    return "standard";
            }
        }

        AiDifficulty aiDifficultyFromString(const std::string& s)
        {
            if (s == "idle")
            {
                return AiDifficulty::Idle;
            }
            if (s == "easy")
            {
                return AiDifficulty::Easy;
            }
            if (s == "hard")
            {
                return AiDifficulty::Hard;
            }
            if (s == "brutal")
            {
                return AiDifficulty::Brutal;
            }
            return AiDifficulty::Standard;
        }

        /**
         * The skirmish options, by name. They have to survive a save because
         * they are rules and not preferences: a game saved with Commander
         * Dies: Game Continues that came back as Game Ends would end on the
         * next commander lost.
         *
         * Read with a default, so a save written before these were stored
         * still loads and resumes on the settings that were in force when it
         * was made -- which were, necessarily, the defaults.
         */
        template <typename T, std::size_t N>
        const char* enumToName(T value, const std::array<std::pair<T, const char*>, N>& names, const char* fallback)
        {
            for (const auto& [v, name] : names)
            {
                if (v == value)
                {
                    return name;
                }
            }
            return fallback;
        }

        template <typename T, std::size_t N>
        T enumFromName(const std::string& s, const std::array<std::pair<T, const char*>, N>& names, T fallback)
        {
            for (const auto& [v, name] : names)
            {
                if (s == name)
                {
                    return v;
                }
            }
            return fallback;
        }

        constexpr std::array<std::pair<LineOfSightMode, const char*>, 3> LineOfSightNames{{
            {LineOfSightMode::Permanent, "permanent"},
            {LineOfSightMode::True, "true"},
            {LineOfSightMode::Circular, "circular"},
        }};

        constexpr std::array<std::pair<MappingMode, const char*>, 2> MappingNames{{
            {MappingMode::Unmapped, "unmapped"},
            {MappingMode::Mapped, "mapped"},
        }};

        constexpr std::array<std::pair<StartLocationMode, const char*>, 2> StartLocationNames{{
            {StartLocationMode::Fixed, "fixed"},
            {StartLocationMode::Random, "random"},
        }};

        constexpr std::array<std::pair<CommanderDeathMode, const char*>, 2> CommanderDeathNames{{
            {CommanderDeathMode::GameEnds, "gameends"},
            {CommanderDeathMode::GameContinues, "gamecontinues"},
        }};
    }

    fs::path getSaveDirectory()
    {
        auto base = getLocalDataPath();
        if (!base)
        {
            throw std::runtime_error("no local data path for saves");
        }
        auto dir = *base / "saves";
        fs::create_directories(dir);
        return dir;
    }

    std::vector<std::string> listSaveGames()
    {
        std::vector<std::pair<fs::file_time_type, std::string>> entries;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(getSaveDirectory(), ec))
        {
            if (entry.path().extension() == ".rwesave")
            {
                entries.emplace_back(entry.last_write_time(ec), entry.path().stem().string());
            }
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<std::string> names;
        names.reserve(entries.size());
        for (const auto& e : entries)
        {
            names.push_back(e.second);
        }
        return names;
    }

    fs::path savePathForName(const std::string& name)
    {
        return getSaveDirectory() / (name + ".rwesave");
    }

    void writeSaveFile(const fs::path& path, const SaveFile& save)
    {
        nlohmann::json j;
        auto& header = j["header"];
        header["mapName"] = save.parameters.mapName;
        header["schemaIndex"] = save.parameters.schemaIndex;
        header["aiDifficulty"] = aiDifficultyToString(save.parameters.aiDifficulty);
        header["lineOfSight"] = enumToName(save.parameters.lineOfSight, LineOfSightNames, "true");
        header["mapping"] = enumToName(save.parameters.mapping, MappingNames, "unmapped");
        header["startLocation"] = enumToName(save.parameters.startLocation, StartLocationNames, "fixed");
        header["commanderDeath"] = enumToName(save.parameters.commanderDeath, CommanderDeathNames, "gameends");
        auto& players = header["players"];
        players = nlohmann::json::array();
        for (const auto& p : save.parameters.players)
        {
            players.push_back(p ? playerToJson(*p) : nlohmann::json());
        }
        if (save.gameTimeSeconds)
        {
            header["gameTimeSeconds"] = *save.gameTimeSeconds;
        }
        j["camera"] = {{"x", save.cameraPosition.x}, {"y", save.cameraPosition.y}, {"z", save.cameraPosition.z}};
        j["sim"] = save.simulation;

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << j.dump();
    }

    std::optional<SaveFile> readSaveFile(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return std::nullopt;
        }
        auto j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.contains("header"))
        {
            return std::nullopt;
        }

        const auto& header = j.at("header");
        GameParameters parameters(header.at("mapName").get<std::string>(), header.at("schemaIndex").get<unsigned int>());
        parameters.aiDifficulty = aiDifficultyFromString(header.value("aiDifficulty", "standard"));
        parameters.lineOfSight = enumFromName(header.value("lineOfSight", "true"), LineOfSightNames, LineOfSightMode::True);
        parameters.mapping = enumFromName(header.value("mapping", "unmapped"), MappingNames, MappingMode::Unmapped);
        parameters.startLocation = enumFromName(header.value("startLocation", "fixed"), StartLocationNames, StartLocationMode::Fixed);
        parameters.commanderDeath = enumFromName(header.value("commanderDeath", "gameends"), CommanderDeathNames, CommanderDeathMode::GameEnds);
        const auto& players = header.at("players");
        for (std::size_t i = 0; i < parameters.players.size() && i < players.size(); ++i)
        {
            if (!players[i].is_null())
            {
                parameters.players[i] = playerFromJson(players[i]);
            }
        }

        SaveFile save(parameters);
        if (header.contains("gameTimeSeconds"))
        {
            save.gameTimeSeconds = header.at("gameTimeSeconds").get<unsigned int>();
        }
        const auto& camera = j.at("camera");
        save.cameraPosition = Vector3f(camera.at("x").get<float>(), camera.at("y").get<float>(), camera.at("z").get<float>());
        save.simulation = j.at("sim");
        return save;
    }

    std::string aiDifficultyDisplayName(AiDifficulty d)
    {
        // Built on the save format's own spelling rather than a second
        // switch, so the two cannot drift apart: this is the same word with
        // its first letter capitalised for the save list's caption style.
        std::string s = aiDifficultyToString(d);
        if (!s.empty())
        {
            s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
        }
        return s;
    }
}
