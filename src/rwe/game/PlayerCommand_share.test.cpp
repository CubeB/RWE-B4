#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <rwe/game/PlayerCommand.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/game/ReplayFile.h>
#include <rwe/pathfinding/PathfindingBackend.h>
#include <rwe/proto/serialization.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/sim_test_util.h>
#include <variant>

/**
 * PlayerShareCommand, the one alternative every resource-sharing action rides
 * in (TOTALA-EXE-COMMANDS.md section 124): across the wire, through a replay
 * file, and past the computer players' command feed.
 */
namespace rwe
{
    namespace
    {
        PlayerShareCommand makeShare(PlayerShareCommand::Kind kind, float amount = 0.0f, std::optional<PlayerId> target = std::nullopt, bool giveMetal = true, bool percent = false)
        {
            PlayerShareCommand c;
            c.kind = kind;
            c.amount = amount;
            c.target = target;
            c.giveMetal = giveMetal;
            c.percent = percent;
            return c;
        }

        PlayerShareCommand roundTrip(const PlayerShareCommand& original)
        {
            proto::PlayerCommand wire;
            serializePlayerCommand(PlayerCommand(original), wire);
            REQUIRE(wire.has_share());

            auto restored = deserializeCommand(wire);
            REQUIRE(std::holds_alternative<PlayerShareCommand>(restored));
            return std::get<PlayerShareCommand>(restored);
        }

        void requireSame(const PlayerShareCommand& a, const PlayerShareCommand& b)
        {
            REQUIRE(a.kind == b.kind);
            REQUIRE(a.amount == b.amount);
            REQUIRE(a.target == b.target);
            REQUIRE(a.giveMetal == b.giveMetal);
            REQUIRE(a.percent == b.percent);
        }
    }

    TEST_CASE("PlayerShareCommand protobuf round-trip", "[share]")
    {
        SECTION("every kind survives, with its amount")
        {
            for (auto kind : {PlayerShareCommand::Kind::ToggleMetal, PlayerShareCommand::Kind::ToggleEnergy, PlayerShareCommand::Kind::SetMetalReserve, PlayerShareCommand::Kind::SetEnergyReserve})
            {
                auto original = makeShare(kind, 123.0f);
                requireSame(roundTrip(original), original);
            }
        }

        SECTION("a gift keeps its target and which resource it is")
        {
            auto metal = makeShare(PlayerShareCommand::Kind::Give, 250.0f, PlayerId(3), true);
            requireSame(roundTrip(metal), metal);

            auto energy = makeShare(PlayerShareCommand::Kind::Give, 75.0f, PlayerId(0), false);
            requireSame(roundTrip(energy), energy);
        }

        SECTION("a negative reserve is carried as typed")
        {
            auto original = makeShare(PlayerShareCommand::Kind::SetEnergyReserve, -40.0f);
            requireSame(roundTrip(original), original);
        }

        SECTION("a percentage reserve keeps its suffix flag")
        {
            auto original = makeShare(PlayerShareCommand::Kind::SetMetalReserve, 50.0f, std::nullopt, true, true);
            requireSame(roundTrip(original), original);
        }

        SECTION("a kind this build does not know does not parse, so it is never taken for a toggle")
        {
            // field 7 (share), length 2: field 1 (kind) as the varint 99.
            // proto2 keeps an unknown enum value as an unknown field, which
            // leaves the required `kind` missing and the message refused,
            // where reading the missing field's default would have flipped
            // the sender's metal toggle.
            const unsigned char bytes[] = {0x3A, 0x02, 0x08, 0x63};
            proto::PlayerCommand wire;
            REQUIRE_FALSE(wire.ParseFromArray(bytes, sizeof(bytes)));
        }
    }

    TEST_CASE("PlayerShareCommand commands survive a replay file", "[share][replay]")
    {
        namespace fs = std::filesystem;
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto path = fs::temp_directory_path() / ("rwe-share-replay-" + std::to_string(stamp) + ".rwereplay");

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
            header.pathfindingBackend = PathfindingBackend::OpenAnnihilation;
            ReplayWriter writer(path, header);
            writer.recordTick(60, PlayerId(1), {makeShare(PlayerShareCommand::Kind::ToggleMetal), makeShare(PlayerShareCommand::Kind::Give, 300.0f, PlayerId(0), false)});
        }

        auto replay = readReplayFile(path);
        std::error_code ec;
        fs::remove(path, ec);

        REQUIRE(replay.has_value());
        const auto& commands = replay->commands.at(60).at(1);
        REQUIRE(commands.size() == 2);
        requireSame(std::get<PlayerShareCommand>(commands[0]), makeShare(PlayerShareCommand::Kind::ToggleMetal));
        requireSame(std::get<PlayerShareCommand>(commands[1]), makeShare(PlayerShareCommand::Kind::Give, 300.0f, PlayerId(0), false));
    }

    TEST_CASE("a computer player's share commands never reach the command stream", "[share]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 100, 3000);
        auto human = addPlayer(sim);

        GamePlayerInfo aiInfo{
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
        auto ai = sim.addPlayer(aiInfo);

        PlayerCommandService service;
        service.registerPlayer(human);
        service.registerPlayer(ai);

        // A marker beside it, so the test can see the feed kept the rest.
        sim.aiPendingCommands[ai].push_back(makeShare(PlayerShareCommand::Kind::ToggleMetal));
        sim.aiPendingCommands[ai].push_back(PlayerSetGameSpeedCommand{4});

        feedAiCommands(sim, service, aiCommandBufferDepth());
        service.pushCommands(human, std::vector<PlayerCommand>());

        auto popped = service.tryPopCommands();
        REQUIRE(popped);
        bool sawMarker = false;
        for (const auto& [id, commands] : *popped)
        {
            for (const auto& c : commands)
            {
                REQUIRE_FALSE(std::holds_alternative<PlayerShareCommand>(c));
                sawMarker = sawMarker || std::holds_alternative<PlayerSetGameSpeedCommand>(c);
            }
        }
        REQUIRE(sawMarker);
    }
}
