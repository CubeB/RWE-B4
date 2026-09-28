#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/TadWriter.h>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/puppet/TadDemo.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/puppet/TadScenePlayback.h>
#include <rwe/puppet/puppet_test_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/sim_test_util.h>
#include <sstream>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path uniqueTempPath()
        {
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            return fs::temp_directory_path() / ("rwe-tad-demo-test-" + std::to_string(stamp) + ".tad");
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

        TadWriterSettings demoSettings()
        {
            TadWriterSettings settings;
            settings.numPlayers = 2;
            settings.maxUnits = 8;
            settings.mapName = "puppet-test";
            settings.recorderVersion = "rwe-test";
            settings.date = "2026-09-27";
            settings.players = {
                TadPlayer{1, static_cast<int8_t>(TadSide::Arm), 1, "A"},
                TadPlayer{2, static_cast<int8_t>(TadSide::Core), 2, "B"},
            };
            settings.dplayIds = {0x11111111u, 0x22222222u};
            settings.playerAddresses = {"127.0.0.1", "::1"};
            return settings;
        }

        std::string writeDemo(const TadWriterSettings& settings, std::size_t unitTypeCount, bool withSpeed)
        {
            std::ostringstream stream;
            TadWriter writer(stream, settings);
            writer.writeHeader();
            writer.writeExtraSectors();
            writer.writePlayers();
            writer.writePlayerStatuses();
            writer.writeUnitTable(unitTypeCount);
            if (withSpeed)
            {
                writer.writePacket(0, 1, {tadEncodeSpeed(TadSpeed{256})});
            }
            writer.close();
            return stream.str();
        }

        void writeTo(const fs::path& path, const std::string& bytes)
        {
            std::ofstream out(path, std::ios::binary);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
    }

    TEST_CASE("a demo's players become remote seats and nobody is local", "[puppet]")
    {
        TadDemoMeta meta;
        meta.header = TadHeader{5, 2, 8, "puppet-test"};
        meta.players = {
            TadPlayer{1, static_cast<int8_t>(TadSide::Arm), 1, "A"},
            TadPlayer{2, static_cast<int8_t>(TadSide::Core), 2, "B"},
        };

        auto params = gameParametersForDemo(meta, "demo.tad");
        REQUIRE(params);
        REQUIRE(params->mapName == "puppet-test");
        REQUIRE(params->tadDemoFile == std::optional<std::string>("demo.tad"));
        REQUIRE(params->players[0]);
        REQUIRE(params->players[1]);
        REQUIRE_FALSE(params->players[2]);
        REQUIRE(std::holds_alternative<PlayerControllerTypeNetwork>(params->players[0]->controller));
        REQUIRE(std::holds_alternative<PlayerControllerTypeNetwork>(params->players[1]->controller));
        REQUIRE(params->players[0]->side == "ARM");
        REQUIRE(params->players[1]->side == "CORE");
    }

    TEST_CASE("a demo seating more players than the engine holds is refused", "[puppet]")
    {
        TadDemoMeta meta;
        meta.header = TadHeader{5, 11, 8, "puppet-test"};
        for (int i = 0; i < 11; ++i)
        {
            meta.players.push_back(TadPlayer{static_cast<uint8_t>(i), static_cast<int8_t>(TadSide::Arm), static_cast<uint8_t>(i), "P"});
        }
        REQUIRE_FALSE(gameParametersForDemo(meta, "demo.tad"));
    }

    TEST_CASE("reading a demo's meta refuses a missing file", "[puppet]")
    {
        REQUIRE_FALSE(readTadDemoMeta("/nonexistent/rwe-tad-demo-test.tad"));
    }

    TEST_CASE("a demo's meta reads its header, players, table and senders", "[puppet]")
    {
        auto bytes = writeDemo(demoSettings(), puppetTestLoadOrder().size(), true);

        TempFile file;
        writeTo(file.path, bytes);

        auto meta = readTadDemoMeta(file.path.string());
        REQUIRE(meta);
        REQUIRE(meta->header);
        REQUIRE(meta->header->mapName == "puppet-test");
        REQUIRE(meta->header->maxUnits == 8);
        REQUIRE(meta->players.size() == 2);
        REQUIRE(meta->unitTable);
        REQUIRE(meta->unitTable->restricted.size() == puppetTestLoadOrder().size());
        REQUIRE(meta->senders == std::vector<uint8_t>{1});
    }

    TEST_CASE("the scene playback refuses a demo whose unit table does not match the data", "[puppet]")
    {
        auto bytes = writeDemo(demoSettings(), 3, false);

        TempFile file;
        writeTo(file.path, bytes);

        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        addWellStockedPlayer(sim, "ARM");
        addWellStockedPlayer(sim, "CORE");

        REQUIRE_THROWS(TadScenePlayback(sim, file.path.string(), {"KBOT", "SOLAR"}));
    }

    TEST_CASE("the scene playback applies a tick and surfaces a recorded speed", "[puppet]")
    {
        auto bytes = writeDemo(demoSettings(), puppetTestLoadOrder().size(), true);

        TempFile file;
        writeTo(file.path, bytes);

        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        addWellStockedPlayer(sim, "ARM");
        addWellStockedPlayer(sim, "CORE");

        TadScenePlayback playback(sim, file.path.string(), puppetTestLoadOrder());
        playback.applyTick(0);

        auto speed = playback.takeSpeedChange();
        REQUIRE(speed);
        REQUIRE(*speed == 256);
        REQUIRE_FALSE(playback.takeSpeedChange());
        REQUIRE(playback.stats().speedChanges == 1);
    }
}
