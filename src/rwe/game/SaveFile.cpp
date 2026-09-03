#include "SaveFile.h"

#include <algorithm>
#include <fstream>
#include <rwe/util.h>
#include <rwe/util/match.h>

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
            return p;
        }

        const char* aiDifficultyToString(AiDifficulty d)
        {
            switch (d)
            {
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
        auto& players = header["players"];
        players = nlohmann::json::array();
        for (const auto& p : save.parameters.players)
        {
            players.push_back(p ? playerToJson(*p) : nlohmann::json());
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
        const auto& players = header.at("players");
        for (std::size_t i = 0; i < parameters.players.size() && i < players.size(); ++i)
        {
            if (!players[i].is_null())
            {
                parameters.players[i] = playerFromJson(players[i]);
            }
        }

        SaveFile save(parameters);
        const auto& camera = j.at("camera");
        save.cameraPosition = Vector3f(camera.at("x").get<float>(), camera.at("y").get<float>(), camera.at("z").get<float>());
        save.simulation = j.at("sim");
        return save;
    }
}
