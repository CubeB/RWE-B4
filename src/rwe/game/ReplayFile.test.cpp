#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <rwe/game/ReplayFile.h>

/**
 * The replay container's round trip.
 *
 * A replay is header plus commands and nothing else, so everything that could
 * go wrong with it is in this file: a lobby field that does not survive the
 * write makes the played-back game a different game, and a command stream that
 * cannot be read past a damaged record throws away the part that was fine.
 */
namespace rwe
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path uniqueTempPath()
        {
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            return fs::temp_directory_path() / ("rwe-replay-test-" + std::to_string(stamp) + ".rwereplay");
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

        PlayerInfo makePlayer(const std::string& name, const std::string& side, unsigned int color, std::optional<int> teamId)
        {
            PlayerInfo p{
                name,
                PlayerControllerTypeHuman(),
                side,
                PlayerColorIndex(color),
                Metal(1000.0f),
                Energy(1000.0f),
                teamId};
            return p;
        }

        ReplayHeader makeHeader()
        {
            ReplayHeader header;
            header.mapName = "Coast To Coast";
            header.schemaIndex = 1;
            header.randomSeed = 424242;
            header.aiDifficulty = AiDifficulty::Brutal;
            header.lineOfSight = LineOfSightMode::Circular;
            header.mapping = MappingMode::Mapped;
            header.startLocation = StartLocationMode::Random;
            header.commanderDeath = CommanderDeathMode::GameContinues;
            return header;
        }

        std::vector<PlayerCommand> unitStop(unsigned int unit)
        {
            return {PlayerUnitCommand(UnitId(unit), PlayerUnitCommand::Stop())};
        }
    }

    TEST_CASE("a replay header survives a round trip", "[replay]")
    {
        TempFile file;

        auto header = makeHeader();
        header.players.push_back(makePlayer("Blue", "ARM", 0, 1));
        header.players.push_back(std::nullopt);
        header.players.push_back(makePlayer("Green", "CORE", 3, 1));
        auto computer = makePlayer("Computer", "CORE", 5, std::nullopt);
        computer.controller = PlayerControllerTypeComputer();
        header.players.push_back(computer);

        ReplayWriter(file.path, header).close();

        auto replay = readReplayFile(file.path);
        REQUIRE(replay.has_value());

        const auto& h = replay->header;
        REQUIRE(h.mapName == "Coast To Coast");
        REQUIRE(h.schemaIndex == 1u);
        REQUIRE(h.randomSeed.has_value());
        REQUIRE(*h.randomSeed == 424242u);
        REQUIRE(h.aiDifficulty == AiDifficulty::Brutal);
        REQUIRE(h.lineOfSight == LineOfSightMode::Circular);
        REQUIRE(h.mapping == MappingMode::Mapped);
        REQUIRE(h.startLocation == StartLocationMode::Random);
        REQUIRE(h.commanderDeath == CommanderDeathMode::GameContinues);

        // The empty slot has to stay an empty slot: a player's index is the
        // PlayerId that the recorded commands name.
        REQUIRE(h.players.size() == 4);
        REQUIRE(!h.players[1].has_value());
        REQUIRE(h.players[0]->name == std::optional<std::string>("Blue"));
        REQUIRE(h.players[0]->side == "ARM");
        REQUIRE(h.players[0]->color.value == 0u);
        REQUIRE(h.players[2]->color.value == 3u);
        REQUIRE(std::holds_alternative<PlayerControllerTypeComputer>(h.players[3]->controller));

        // The gap SaveFile's header has and this one must not: an allied game
        // replayed without its alliances is a free-for-all.
        REQUIRE(h.players[0]->teamId == std::optional<int>(1));
        REQUIRE(h.players[2]->teamId == std::optional<int>(1));
        REQUIRE(!h.players[3]->teamId.has_value());
    }

    TEST_CASE("a replay header rebuilds the game parameters it came from", "[replay]")
    {
        TempFile file;

        GameParameters parameters("Great Divide", 2);
        parameters.randomSeed = 7;
        parameters.commanderDeath = CommanderDeathMode::GameContinues;
        parameters.players[0] = makePlayer("Blue", "ARM", 0, 2);
        parameters.players[1] = makePlayer("Red", "CORE", 1, 2);

        ReplayWriter(file.path, replayHeaderFromParameters(parameters)).close();

        auto replay = readReplayFile(file.path);
        REQUIRE(replay.has_value());
        auto restored = gameParametersFromReplayHeader(replay->header);

        REQUIRE(restored.mapName == "Great Divide");
        REQUIRE(restored.schemaIndex == 2u);
        REQUIRE(restored.randomSeed == std::optional<unsigned int>(7));
        REQUIRE(restored.commanderDeath == CommanderDeathMode::GameContinues);
        REQUIRE(restored.players[0]->teamId == std::optional<int>(2));
        REQUIRE(restored.players[1]->teamId == std::optional<int>(2));
        REQUIRE(!restored.players[2].has_value());
    }

    TEST_CASE("commands survive a round trip across ticks and players", "[replay]")
    {
        TempFile file;

        {
            ReplayWriter writer(file.path, makeHeader());
            writer.recordTick(10, PlayerId(0), unitStop(4));
            writer.recordTick(10, PlayerId(1), {PlayerPauseGameCommand()});
            writer.recordTick(11, PlayerId(0), {});
            writer.recordTick(12, PlayerId(0), {PlayerSetGameSpeedCommand{3}, PlayerUnitCommand(UnitId(9), PlayerUnitCommand::IssueOrder(MoveOrder(SimVector(1_ss, 2_ss, 3_ss)), PlayerUnitCommand::IssueOrder::Queued))});
        }

        auto replay = readReplayFile(file.path);
        REQUIRE(replay.has_value());
        REQUIRE(replay->lastTick == 12u);

        // Tick 11 had nothing in it, so it is not in the file and not in the map.
        REQUIRE(replay->commands.size() == 2);
        REQUIRE(replay->commands.count(11) == 0);

        const auto& tick10 = replay->commands.at(10);
        REQUIRE(tick10.size() == 2);
        REQUIRE(tick10.at(0).size() == 1);
        const auto& stop = std::get<PlayerUnitCommand>(tick10.at(0)[0]);
        REQUIRE(stop.unit == UnitId(4));
        REQUIRE(std::holds_alternative<PlayerUnitCommand::Stop>(stop.command));
        REQUIRE(std::holds_alternative<PlayerPauseGameCommand>(tick10.at(1)[0]));

        const auto& tick12 = replay->commands.at(12);
        REQUIRE(tick12.size() == 1);
        REQUIRE(tick12.at(0).size() == 2);
        REQUIRE(std::get<PlayerSetGameSpeedCommand>(tick12.at(0)[0]).speedIndex == 3);
        const auto& move = std::get<PlayerUnitCommand>(tick12.at(0)[1]);
        REQUIRE(move.unit == UnitId(9));
        const auto& issue = std::get<PlayerUnitCommand::IssueOrder>(move.command);
        REQUIRE(issue.issueKind == PlayerUnitCommand::IssueOrder::Queued);
        const auto& destination = std::get<MoveOrder>(issue.order).destination;
        REQUIRE(destination.x.value == 1.0f);
        REQUIRE(destination.y.value == 2.0f);
        REQUIRE(destination.z.value == 3.0f);
    }

    TEST_CASE("a replay with no commands in it still reads", "[replay]")
    {
        TempFile file;

        {
            ReplayWriter writer(file.path, makeHeader());
            writer.recordTick(1, PlayerId(0), {});
            writer.recordTick(2, PlayerId(1), {});
        }

        auto replay = readReplayFile(file.path);
        REQUIRE(replay.has_value());
        REQUIRE(replay->header.mapName == "Coast To Coast");
        REQUIRE(replay->commands.empty());
        // Nothing was ordered, but the game still ran to tick 2, and that is
        // the length a scrub bar has to span. A replay that ended at its last
        // command would stop wherever the players last did something.
        REQUIRE(replay->lastTick == 2u);
    }

    TEST_CASE("a replay truncated mid-record keeps everything before the cut", "[replay]")
    {
        TempFile file;

        std::uintmax_t sizeAfterTwoRecords = 0;
        {
            ReplayWriter writer(file.path, makeHeader());
            writer.recordTick(5, PlayerId(0), unitStop(1));
            writer.recordTick(6, PlayerId(0), unitStop(2));
            // Readable from under the open writer only because every record is
            // flushed as it is written, which is the whole of the crash-safety
            // claim.
            sizeAfterTwoRecords = fs::file_size(file.path);
            writer.recordTick(7, PlayerId(0), unitStop(3));
        }

        auto whole = readReplayFile(file.path);
        REQUIRE(whole.has_value());
        REQUIRE(whole->lastTick == 7u);

        SECTION("cut inside the third record's payload")
        {
            // Measured from the end of the second record rather than from the
            // end of the file, because the file now ends with the end-of-game
            // marker and trimming a byte off that would only lose the length.
            fs::resize_file(file.path, sizeAfterTwoRecords + 13);
            auto replay = readReplayFile(file.path);
            REQUIRE(replay.has_value());
            REQUIRE(replay->lastTick == 6u);
            REQUIRE(replay->commands.size() == 2);
            REQUIRE(replay->commands.count(7) == 0);
        }

        SECTION("cut so that only the end-of-game marker is lost")
        {
            // The commands all survive; what goes is the recording's own idea
            // of how long it was, which falls back to the last command.
            fs::resize_file(file.path, fs::file_size(file.path) - 1);
            auto replay = readReplayFile(file.path);
            REQUIRE(replay.has_value());
            REQUIRE(replay->commands.size() == 3);
            REQUIRE(replay->lastTick == 7u);
        }

        SECTION("cut inside the third record's fixed fields")
        {
            // The other half of the truncation case: the cut lands before the
            // length field, so the payload's own short-read check never gets a
            // chance to fire.
            fs::resize_file(file.path, sizeAfterTwoRecords + 6);
            auto replay = readReplayFile(file.path);
            REQUIRE(replay.has_value());
            REQUIRE(replay->lastTick == 6u);
        }

        SECTION("cut in the header, which leaves nothing to play")
        {
            fs::resize_file(file.path, 20);
            REQUIRE(!readReplayFile(file.path).has_value());
        }
    }

    TEST_CASE("a file that is not a replay reads as nothing", "[replay]")
    {
        TempFile file;

        SECTION("a file that does not exist")
        {
            REQUIRE(!readReplayFile(file.path).has_value());
        }

        SECTION("a file with the wrong magic")
        {
            std::ofstream out(file.path, std::ios::binary);
            out << "RWESAVE!!" << '\1' << "whatever follows";
            out.close();
            REQUIRE(!readReplayFile(file.path).has_value());
        }

        SECTION("a replay from a version this build cannot read")
        {
            ReplayWriter(file.path, makeHeader()).close();
            std::fstream out(file.path, std::ios::binary | std::ios::in | std::ios::out);
            out.seekp(9);
            out.put('\x7f');
            out.close();
            REQUIRE(!readReplayFile(file.path).has_value());
        }

        SECTION("a file too short to hold even the magic")
        {
            std::ofstream out(file.path, std::ios::binary);
            out << "RWE";
            out.close();
            REQUIRE(!readReplayFile(file.path).has_value());
        }
    }
}
