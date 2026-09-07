#include "ReplayFile.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <network.pb.h>
#include <nlohmann/json.hpp>
#include <rwe/proto/serialization.h>
#include <rwe/util/match.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace rwe
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr char Magic[] = {'R', 'W', 'E', 'R', 'E', 'P', 'L', 'A', 'Y'};
        constexpr std::size_t MagicSize = sizeof(Magic);
        constexpr char Version = 1;

        /**
         * A serialized command runs to tens of bytes; the longest carries a
         * unit type name. Anything claiming a megabyte is a length field read
         * out of garbage, and honouring it would mean allocating whatever a
         * corrupted byte happened to say.
         */
        constexpr std::uint32_t MaxRecordSize = 1u << 20;

        /**
         * Written in the player field of the final record, which carries no
         * payload and says how long the game ran. A real player id is a slot
         * index, so this cannot collide with one.
         */
        constexpr std::uint32_t EndOfGameMarker = 0xFFFFFFFFu;

        /**
         * Written a byte at a time on purpose. The engine's saves and replays
         * are meant to be readable on a machine other than the one that wrote
         * them, and a struct blitted out of memory is neither portable across
         * endianness nor safe against a compiler's padding.
         */
        void writeUint32Le(std::ostream& out, std::uint32_t value)
        {
            const char bytes[4] = {
                static_cast<char>(value & 0xFFu),
                static_cast<char>((value >> 8) & 0xFFu),
                static_cast<char>((value >> 16) & 0xFFu),
                static_cast<char>((value >> 24) & 0xFFu)};
            out.write(bytes, 4);
        }

        bool readUint32Le(std::istream& in, std::uint32_t& value)
        {
            unsigned char bytes[4];
            in.read(reinterpret_cast<char*>(bytes), 4);
            if (in.gcount() != 4)
            {
                return false;
            }
            value = static_cast<std::uint32_t>(bytes[0])
                | (static_cast<std::uint32_t>(bytes[1]) << 8)
                | (static_cast<std::uint32_t>(bytes[2]) << 16)
                | (static_cast<std::uint32_t>(bytes[3]) << 24);
            return true;
        }

        /**
         * The lobby options by name, spelled exactly as SaveFile spells them.
         * A replay and a save describe the same lobby, and someone reading one
         * file after the other should not have to learn a second vocabulary.
         *
         * Read with a default throughout, so a replay written before a field
         * existed still plays back on whatever was in force when it was made.
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

        constexpr std::array<std::pair<AiDifficulty, const char*>, 5> AiDifficultyNames{{
            {AiDifficulty::Idle, "idle"},
            {AiDifficulty::Easy, "easy"},
            {AiDifficulty::Standard, "standard"},
            {AiDifficulty::Hard, "hard"},
            {AiDifficulty::Brutal, "brutal"},
        }};

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

        nlohmann::json playerToJson(const PlayerInfo& p)
        {
            nlohmann::json j;
            if (p.name)
            {
                j["name"] = *p.name;
            }
            // The kind of controller, but never a network player's host and
            // port: those say where the game was played, and a replay is
            // watched somewhere else entirely.
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
                j["team"] = *p.teamId;
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
                Energy(j.at("energy").get<float>()),
                std::nullopt};
            if (j.contains("name"))
            {
                p.name = j.at("name").get<std::string>();
            }
            if (j.contains("team"))
            {
                p.teamId = j.at("team").get<int>();
            }
            auto controller = j.value("controller", "human");
            if (controller == "computer")
            {
                p.controller = PlayerControllerTypeComputer();
            }
            else if (controller == "network")
            {
                p.controller = PlayerControllerTypeNetwork();
            }
            return p;
        }

        nlohmann::json headerToJson(const ReplayHeader& header)
        {
            nlohmann::json j;
            j["mapName"] = header.mapName;
            j["schemaIndex"] = header.schemaIndex;
            if (header.randomSeed)
            {
                j["randomSeed"] = *header.randomSeed;
            }
            j["aiDifficulty"] = enumToName(header.aiDifficulty, AiDifficultyNames, "standard");
            j["lineOfSight"] = enumToName(header.lineOfSight, LineOfSightNames, "true");
            j["mapping"] = enumToName(header.mapping, MappingNames, "unmapped");
            j["startLocation"] = enumToName(header.startLocation, StartLocationNames, "fixed");
            j["commanderDeath"] = enumToName(header.commanderDeath, CommanderDeathNames, "gameends");
            auto& players = j["players"];
            players = nlohmann::json::array();
            for (const auto& p : header.players)
            {
                players.push_back(p ? playerToJson(*p) : nlohmann::json());
            }
            return j;
        }

        std::optional<ReplayHeader> headerFromJson(const nlohmann::json& j)
        {
            if (!j.is_object() || !j.contains("mapName") || !j.contains("players"))
            {
                return std::nullopt;
            }

            ReplayHeader header;
            header.mapName = j.at("mapName").get<std::string>();
            header.schemaIndex = j.value("schemaIndex", 0u);
            if (j.contains("randomSeed"))
            {
                header.randomSeed = j.at("randomSeed").get<unsigned int>();
            }
            header.aiDifficulty = enumFromName(j.value("aiDifficulty", "standard"), AiDifficultyNames, AiDifficulty::Standard);
            header.lineOfSight = enumFromName(j.value("lineOfSight", "true"), LineOfSightNames, LineOfSightMode::True);
            header.mapping = enumFromName(j.value("mapping", "unmapped"), MappingNames, MappingMode::Unmapped);
            header.startLocation = enumFromName(j.value("startLocation", "fixed"), StartLocationNames, StartLocationMode::Fixed);
            header.commanderDeath = enumFromName(j.value("commanderDeath", "gameends"), CommanderDeathNames, CommanderDeathMode::GameEnds);
            for (const auto& p : j.at("players"))
            {
                header.players.push_back(p.is_null() ? std::optional<PlayerInfo>() : std::optional<PlayerInfo>(playerFromJson(p)));
            }
            return header;
        }
    }

    ReplayHeader replayHeaderFromParameters(const GameParameters& parameters)
    {
        ReplayHeader header;
        header.mapName = parameters.mapName;
        header.schemaIndex = parameters.schemaIndex;
        header.randomSeed = parameters.randomSeed;
        header.aiDifficulty = parameters.aiDifficulty;
        header.lineOfSight = parameters.lineOfSight;
        header.mapping = parameters.mapping;
        header.startLocation = parameters.startLocation;
        header.commanderDeath = parameters.commanderDeath;
        for (const auto& p : parameters.players)
        {
            header.players.push_back(p);
        }
        return header;
    }

    GameParameters gameParametersFromReplayHeader(const ReplayHeader& header)
    {
        GameParameters parameters(header.mapName, header.schemaIndex);
        parameters.randomSeed = header.randomSeed;
        parameters.aiDifficulty = header.aiDifficulty;
        parameters.lineOfSight = header.lineOfSight;
        parameters.mapping = header.mapping;
        parameters.startLocation = header.startLocation;
        parameters.commanderDeath = header.commanderDeath;
        for (std::size_t i = 0; i < parameters.players.size() && i < header.players.size(); ++i)
        {
            parameters.players[i] = header.players[i];
        }
        return parameters;
    }

    ReplayWriter::ReplayWriter(const fs::path& path, const ReplayHeader& header)
        : out(path, std::ios::binary | std::ios::trunc)
    {
        if (!out)
        {
            throw std::runtime_error("failed to open replay file for writing: " + path.string());
        }

        out.write(Magic, MagicSize);
        out.put(Version);

        auto headerJson = headerToJson(header).dump();
        writeUint32Le(out, static_cast<std::uint32_t>(headerJson.size()));
        out.write(headerJson.data(), static_cast<std::streamsize>(headerJson.size()));
        out.flush();
    }

    ReplayWriter::~ReplayWriter()
    {
        close();
    }

    void ReplayWriter::recordTick(unsigned int tick, PlayerId player, const std::vector<PlayerCommand>& commands)
    {
        if (!out.is_open())
        {
            return;
        }

        lastTickSeen = std::max(lastTickSeen, tick);

        for (const auto& command : commands)
        {
            proto::PlayerCommand proto;
            serializePlayerCommand(command, proto);
            auto payload = proto.SerializeAsString();

            writeUint32Le(out, tick);
            writeUint32Le(out, player.value);
            writeUint32Le(out, static_cast<std::uint32_t>(payload.size()));
            out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        }

        if (!commands.empty())
        {
            out.flush();
        }
    }

    void ReplayWriter::close()
    {
        if (!out.is_open())
        {
            return;
        }

        // How long the game ran, as a record with no player and no payload.
        // Without it a replay ends at its last command, which on a quiet
        // game is minutes early -- the scrub bar would stop somewhere in the
        // middle and the playback would look like it had crashed.
        writeUint32Le(out, lastTickSeen);
        writeUint32Le(out, EndOfGameMarker);
        writeUint32Le(out, 0u);
        out.flush();
        out.close();
    }

    std::optional<Replay> readReplayFile(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return std::nullopt;
        }

        char magic[MagicSize];
        in.read(magic, MagicSize);
        if (in.gcount() != static_cast<std::streamsize>(MagicSize) || std::memcmp(magic, Magic, MagicSize) != 0)
        {
            return std::nullopt;
        }

        char version;
        in.read(&version, 1);
        if (in.gcount() != 1 || version != Version)
        {
            return std::nullopt;
        }

        std::uint32_t headerSize;
        if (!readUint32Le(in, headerSize) || headerSize > MaxRecordSize)
        {
            return std::nullopt;
        }
        std::string headerJson(headerSize, '\0');
        in.read(headerJson.data(), static_cast<std::streamsize>(headerSize));
        if (in.gcount() != static_cast<std::streamsize>(headerSize))
        {
            return std::nullopt;
        }

        auto parsed = nlohmann::json::parse(headerJson, nullptr, false);
        if (parsed.is_discarded())
        {
            return std::nullopt;
        }
        auto header = headerFromJson(parsed);
        if (!header)
        {
            return std::nullopt;
        }

        Replay replay;
        replay.header = std::move(*header);

        // Every exit from here on keeps what has been read so far. A replay
        // that ends mid-record is a game that ended mid-record, which is the
        // ordinary way for one to end.
        std::string payload;
        while (true)
        {
            std::uint32_t tick;
            std::uint32_t playerId;
            std::uint32_t payloadSize;
            if (!readUint32Le(in, tick) || !readUint32Le(in, playerId) || !readUint32Le(in, payloadSize))
            {
                break;
            }

            if (playerId == EndOfGameMarker)
            {
                // How long the game ran. It is the last thing written, so
                // there is nothing after it worth looking for.
                replay.lastTick = std::max(replay.lastTick, tick);
                break;
            }
            if (payloadSize > MaxRecordSize)
            {
                break;
            }

            payload.resize(payloadSize);
            in.read(payload.data(), static_cast<std::streamsize>(payloadSize));
            if (in.gcount() != static_cast<std::streamsize>(payloadSize))
            {
                break;
            }

            proto::PlayerCommand proto;
            if (!proto.ParseFromArray(payload.data(), static_cast<int>(payloadSize)))
            {
                break;
            }

            // A payload that parses can still be a command this build has no
            // case for, and deserializeCommand throws on one. That is the same
            // kind of damage as a short read and gets the same treatment.
            try
            {
                replay.commands[tick][playerId].push_back(deserializeCommand(proto));
            }
            catch (const std::exception&)
            {
                break;
            }

            replay.lastTick = tick;
        }

        return replay;
    }
}
