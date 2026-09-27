#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/puppet/puppet_test_util.h>
#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <sstream>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        struct ReplayHandler : TadHandler
        {
            TadPuppetDriver* driver{nullptr};
            std::optional<TadHeader> header;
            std::vector<TadPlayer> players;
            std::vector<uint8_t> senders;

            void onHeader(const TadHeader& h) override { header = h; }
            void onPlayer(const TadPlayer& p, unsigned int, unsigned int) override { players.push_back(p); }
            void onUnitData(const TadBytes&) override {}

            void onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets, const TadWalkStats&) override
            {
                if (std::find(senders.begin(), senders.end(), packet.sender) == senders.end())
                {
                    senders.push_back(packet.sender);
                }
                if (driver != nullptr)
                {
                    driver->onPacket(packet, subPackets);
                }
            }
        };

        int countFeatures(const GameSimulation& sim)
        {
            int n = 0;
            for ([[maybe_unused]] const auto& f : sim.features)
            {
                ++n;
            }
            return n;
        }
    }

    TEST_CASE("a recorded demo plays back through the puppet driver", "[puppet]")
    {
        GameSimulation rec(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(rec);
        auto arm = addWellStockedPlayer(rec, "ARM");

        DemoRecorderSettings settings;
        settings.maxUnits = 8;
        settings.mapName = "puppet-test";
        settings.unitLoadOrder = puppetTestLoadOrder();

        std::ostringstream stream;
        rec.attachDemoRecorder(std::make_unique<DemoRecorder>(stream, rec, settings));

        auto tank = rec.trySpawnCompletedUnit("TANK", arm, SimVector(0_ss, 0_ss, 300_ss), std::nullopt);
        REQUIRE(tank);
        auto tankStart = rec.getUnitState(*tank).position;
        rec.getUnitState(*tank).orders.push_back(MoveOrder(SimVector(300_ss, 0_ss, 300_ss)));

        auto kbot = rec.trySpawnCompletedUnit("KBOT", arm, SimVector(-60_ss, 0_ss, 0_ss), std::nullopt);
        REQUIRE(kbot);

        // The recorder's own build-start hook, which the simulation's builder
        // path calls when a frame goes down; the frame is finished by hand
        // because the write side here is about the stream, not the AI.
        auto solar = rec.trySpawnUnit("SOLAR", arm, SimVector(40_ss, 0_ss, 40_ss), std::nullopt);
        REQUIRE(solar);
        rec.demoRecorder->buildStarted(rec, *kbot, *solar);
        tick(rec, 5);
        rec.getUnitState(*solar).finishBuilding(rec.unitDefinitions.at("SOLAR"));
        tick(rec, 5);

        tick(rec, 600);

        auto solarFound = findUnitOfType(rec, "SOLAR");
        REQUIRE(solarFound);
        auto recMoved = rec.getUnitState(*tank).position != tankStart;
        REQUIRE(recMoved);

        rec.killUnit(*solarFound);
        tick(rec, 3);

        rec.demoRecorder->close();
        auto demo = stream.str();
        REQUIRE(!demo.empty());

        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        addWellStockedPlayer(sim, "ARM");

        TadPuppetDriver driver(sim, settings.maxUnits, settings.unitLoadOrder);

        ReplayHandler meta;
        std::istringstream in(demo);
        readTad(in, meta);
        REQUIRE(meta.header);
        REQUIRE(meta.senders.size() == 1);

        driver.addPlayer(meta.senders.front(), PlayerId(0));

        ReplayHandler play;
        play.driver = &driver;
        std::istringstream again(demo);
        readTad(again, play);

        auto playedTank = findUnitOfType(sim, "TANK");
        REQUIRE(playedTank);
        auto puppetMoved = sim.getUnitState(*playedTank).position != tankStart;
        REQUIRE(puppetMoved);
        REQUIRE_FALSE(findUnitOfType(sim, "SOLAR"));
        REQUIRE(countFeatures(sim) == 1);

        REQUIRE(driver.stats().groundDrift.samples > 0);
        for (auto d : driver.stats().groundDrift.distances)
        {
            REQUIRE(d <= 32.0);
        }
    }

    TEST_CASE("the external clock holds a record until the scene's tick", "[puppet]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        addWellStockedPlayer(sim, "ARM");

        TadPuppetDriver driver(sim, 8, puppetTestLoadOrder());
        driver.addPlayer(1, PlayerId(0));
        driver.setExternalClock(true);

        auto layout = tadUnitStateLayout(std::vector<bool>{false, false, false}, 8);

        TadUnitState place;
        place.tick = 0;
        place.sync = TadUnitSync{0, 3, 100, 0, 0, 0, std::nullopt, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}, std::nullopt};
        driver.onPacket(TadPacket{0, 1}, {tadEncodeUnitState(place, layout)});

        // Queued, not applied: the driver does not own the clock.
        REQUIRE_FALSE(findUnitOfType(sim, "TANK"));

        driver.applyTick(0);
        REQUIRE(findUnitOfType(sim, "TANK"));
        REQUIRE(driver.lastTick() == 0u);

        // A record for a later tick stays queued until its tick comes round.
        // The slot is tick % maxUnits, so the later record is at tick 8.
        auto firstPosition = sim.getUnitState(*findUnitOfType(sim, "TANK")).position;
        TadUnitState later;
        later.tick = 8;
        later.sync = TadUnitSync{0, 3, 50, 0, 0, 0, std::nullopt, TadPosition{2000000, 0, 0}, TadRotation{0, 0, 0}, std::nullopt};
        driver.onPacket(TadPacket{0, 1}, {tadEncodeUnitState(later, layout)});
        driver.applyTick(1);
        REQUIRE(sim.getUnitState(*findUnitOfType(sim, "TANK")).position.x == firstPosition.x);
        driver.applyTick(8);
        REQUIRE(sim.getUnitState(*findUnitOfType(sim, "TANK")).hitPoints == 50);
        REQUIRE(driver.lastTick() == 8u);
    }
}
