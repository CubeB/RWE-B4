#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/util/rwe_string.h>
#include <sstream>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        struct RecordingHandler : TadHandler
        {
            std::optional<TadHeader> header;
            std::vector<TadPlayer> players;
            std::vector<TadPlayerStatus> statuses;
            std::vector<TadUnitTableEntry> unitTable;
            std::vector<TadPacket> packets;
            std::vector<std::vector<TadBytes>> subPackets;
            TadWalkStats stats;

            void onHeader(const TadHeader& h) override { header = h; }
            void onPlayer(const TadPlayer& p, unsigned int, unsigned int) override { players.push_back(p); }
            void onPlayerStatus(const TadPlayerStatus& s, unsigned int, unsigned int) override { statuses.push_back(s); }

            void onUnitData(const TadBytes& d) override
            {
                if (auto table = tadDecodeUnitTable(d))
                {
                    unitTable = table->restricted;
                }
            }

            void onPacket(const TadPacket& p, const std::vector<TadBytes>& subs, const TadWalkStats& s) override
            {
                packets.push_back(p);
                subPackets.push_back(subs);
                stats.unknownCodes += s.unknownCodes;
                stats.truncated += s.truncated;
                stats.failedDecompressions += s.failedDecompressions;
            }
        };

        RecordingHandler readDemo(const std::string& demo)
        {
            std::istringstream stream(demo);
            RecordingHandler handler;
            readTad(stream, handler);
            return handler;
        }

        std::optional<TadBytes> firstSubPacketOf(const std::vector<TadBytes>& subs, uint8_t code)
        {
            for (const auto& sub : subs)
            {
                if (!sub.empty() && sub[0] == code)
                {
                    return sub;
                }
            }
            return std::nullopt;
        }

        /** The index of the first subpacket with this code, or -1. */
        int subPacketPosition(const std::vector<TadBytes>& subs, uint8_t code)
        {
            for (std::size_t i = 0; i < subs.size(); ++i)
            {
                if (!subs[i].empty() && subs[i][0] == code)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        UnitDefinition makeGroundUnit(unsigned int buildTime)
        {
            UnitDefinition definition{};
            definition.buildTime = buildTime;
            definition.maxHitPoints = 100;
            definition.maxVelocity = 1_ss;
            definition.isMobile = true;
            return definition;
        }

        UnitDefinition makeBuilding(unsigned int buildTime)
        {
            UnitDefinition definition{};
            definition.buildTime = buildTime;
            definition.maxHitPoints = 100;
            return definition;
        }

        DemoRecorderSettings makeDemoSettings()
        {
            DemoRecorderSettings settings;
            settings.maxUnits = 4;
            settings.mapName = "Coast To Coast";
            // The order the loader would have captured, as if sorted by stem.
            settings.unitLoadOrder = {"ARMCOM", "ARMSOLAR", "ARMPEEP"};
            return settings;
        }

        TadUnitStateLayout makeDemoLayout()
        {
            // canFly in load order: the peeper flies, the others do not.
            return tadUnitStateLayout({false, false, true}, 4);
        }
    }

    TEST_CASE("DemoIdAllocator", "[demorecorder]")
    {
        SECTION("numbers ids by block arithmetic, one block per owner")
        {
            DemoIdAllocator ids(1000);
            PlayerId a(0);
            PlayerId b(1);

            REQUIRE(ids.allocate(a, UnitId(10)) == 1);
            REQUIRE(ids.allocate(a, UnitId(11)) == 2);
            REQUIRE(ids.allocate(b, UnitId(12)) == 1001);

            REQUIRE(ids.idOf(UnitId(10)) == 1);
            REQUIRE(ids.indexOf(UnitId(10)) == 0);
            REQUIRE(ids.indexOf(UnitId(11)) == 1);
            REQUIRE(ids.ownerOf(UnitId(12)) == b);
            REQUIRE(ids.usedBy(a) == 2);
            REQUIRE(ids.usedBy(b) == 1);
            REQUIRE_FALSE(ids.idOf(UnitId(99)));
            REQUIRE_FALSE(ids.ownerOf(UnitId(99)));
        }

        SECTION("recycles a released slot, lowest first")
        {
            DemoIdAllocator ids(1000);
            PlayerId a(0);

            REQUIRE(ids.allocate(a, UnitId(1)) == 1);
            REQUIRE(ids.allocate(a, UnitId(2)) == 2);
            REQUIRE(ids.allocate(a, UnitId(3)) == 3);

            ids.release(UnitId(2));
            ids.release(UnitId(1));
            REQUIRE(ids.usedBy(a) == 1);

            REQUIRE(ids.allocate(a, UnitId(4)) == 1);
            REQUIRE(ids.allocate(a, UnitId(5)) == 2);
            REQUIRE(ids.allocate(a, UnitId(6)) == 4);
        }

        SECTION("refuses once a player's block is full rather than wrapping")
        {
            DemoIdAllocator ids(2);
            PlayerId a(0);
            PlayerId b(1);

            REQUIRE(ids.allocate(a, UnitId(1)) == 1);
            REQUIRE(ids.allocate(a, UnitId(2)) == 2);
            REQUIRE_FALSE(ids.allocate(a, UnitId(3)));

            // The refusal is the owner's own block, not the allocator.
            REQUIRE(ids.allocate(b, UnitId(3)) == 3);

            ids.release(UnitId(1));
            REQUIRE(ids.allocate(a, UnitId(4)) == 1);
        }

        SECTION("hands a unit that already has an id the same one back")
        {
            DemoIdAllocator ids(1000);
            PlayerId a(0);

            REQUIRE(ids.allocate(a, UnitId(1)) == 1);
            REQUIRE(ids.allocate(a, UnitId(1)) == 1);
            REQUIRE(ids.usedBy(a) == 1);
        }

        SECTION("releasing a unit it has never seen is a no-op")
        {
            DemoIdAllocator ids(1000);
            ids.release(UnitId(1));
            REQUIRE(ids.usedBy(PlayerId(0)) == 0);
        }
    }

    TEST_CASE("DemoRecorder", "[demorecorder]")
    {
        GameSimulation simulation(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto arm = addWellStockedPlayer(simulation, "ARM");
        auto core = addWellStockedPlayer(simulation, "CORE");
        simulation.unitDefinitions["ARMCOM"] = makeGroundUnit(100);
        simulation.unitDefinitions["ARMSOLAR"] = makeBuilding(100);
        simulation.unitDefinitions["ARMPEEP"] = makeGroundUnit(100);
        simulation.unitDefinitions["ARMPEEP"].canFly = true;

        // One ground unit on the ARM side and one peeper on the CORE side,
        // so both senders have a block and the two serialisers are both used.
        auto armCommander = addUnitOfType(simulation, "ARMCOM", arm, SimVector(100_ss, 0_ss, 200_ss), makeEmptyCobScript());
        [[maybe_unused]] auto corePeeper = addUnitOfType(simulation, "ARMPEEP", core, SimVector(300_ss, 0_ss, 400_ss), makeEmptyCobScript());

        std::ostringstream stream;
        DemoRecorder recorder(stream, simulation, makeDemoSettings());

        auto layout = makeDemoLayout();

        SECTION("writes the container and one 0x2c per sender per tick")
        {
            simulation.gameTime += GameTime(4);
            recorder.endOfTick(simulation);
            recorder.close();

            auto handler = readDemo(stream.str());
            REQUIRE(handler.stats.total() == 0);
            REQUIRE(handler.header);
            REQUIRE(handler.header->version == 5);
            REQUIRE(handler.header->numPlayers == 2);
            REQUIRE(handler.header->maxUnits == 4);
            REQUIRE(handler.header->mapName == "Coast To Coast");

            REQUIRE(handler.players.size() == 2);
            REQUIRE(handler.players[0].name == "player");
            REQUIRE(handler.players[0].side == static_cast<int8_t>(TadSide::Arm));
            REQUIRE(handler.players[1].side == static_cast<int8_t>(TadSide::Core));

            // The status ids run from one so a 0x0c can name a player.
            REQUIRE(handler.statuses.size() == 2);
            REQUIRE(handler.statuses[0].checksum.matches());
            REQUIRE(handler.statuses[0].dplayId == 1);
            REQUIRE(handler.statuses[1].dplayId == 2);

            REQUIRE(handler.unitTable.size() == 3);

            // Two senders, one packet each, both at the tick the recorder
            // last saw, and each carrying its own unit state.
            REQUIRE(handler.packets.size() == 2);
            REQUIRE(handler.packets[0].sender == 0);
            REQUIRE(handler.packets[1].sender == 1);
            for (const auto& subs : handler.subPackets)
            {
                auto sub = firstSubPacketOf(subs, static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove));
                REQUIRE(sub);
                auto state = tadDecodeUnitState(*sub, layout);
                REQUIRE(state);
                REQUIRE(state->tick == 4);
                REQUIRE(state->sync);
            }

            // Tick 4 puts ARM's index 0 (the commander) up for its full state.
            auto armState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(armState->sync->index == 0);
            REQUIRE(armState->sync->typeIndex == 1);
            REQUIRE(armState->sync->health == 100);
            REQUIRE(armState->sync->buildProgress == 0);
            REQUIRE(armState->sync->speed);
            REQUIRE(armState->sync->position == TadPosition{simScalarToFixed(100_ss), 0, simScalarToFixed(200_ss)});

            // The peeper's block is index 0 too, being its owner's first unit.
            auto coreState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(coreState->sync->index == 0);
            REQUIRE(coreState->sync->typeIndex == 3);
        }

        SECTION("decodes a ground mover from an empty path and an air mover from its mode")
        {
            simulation.gameTime += GameTime(1);
            recorder.endOfTick(simulation);
            recorder.close();

            auto handler = readDemo(stream.str());

            // Tick 1 puts index 1 up for a full state on each side; the
            // updates carry one entry each because neither unit has been
            // sent before.
            auto armState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(armState);
            REQUIRE(armState->updates.size() == 1);
            REQUIRE(armState->updates[0].index == 0);
            REQUIRE(armState->updates[0].typeIndex == 1);
            auto* ground = std::get_if<TadGroundPath>(&armState->updates[0].mover);
            REQUIRE(ground);
            REQUIRE(ground->waypoints.empty());
            REQUIRE(armState->sync->index == 1);
            REQUIRE(armState->sync->typeIndex == 0);

            auto coreState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(coreState);
            REQUIRE(coreState->updates.size() == 1);
            auto* air = std::get_if<TadAirMover>(&coreState->updates[0].mover);
            REQUIRE(air);
            REQUIRE(std::holds_alternative<std::monostate>(air->goal));
            REQUIRE(air->movementMode == 1);
        }

        SECTION("sends 0x09 before the 0x2c, and 0x12 with the remembered builder")
        {
            auto frame = addUnitOfType(simulation, "ARMSOLAR", arm, SimVector(140_ss, 0_ss, 200_ss), makeEmptyCobScript());
            simulation.getUnitState(frame).buildTimeCompleted = 0;
            recorder.unitCreated(simulation, frame);

            simulation.gameTime += GameTime(1);
            recorder.buildStarted(simulation, armCommander, frame);
            recorder.endOfTick(simulation);

            {
                auto handler = readDemo(stream.str());
                REQUIRE(handler.packets.size() == 2);
                const auto& subs = handler.subPackets[0];

                auto buildStartedPos = subPacketPosition(subs, static_cast<uint8_t>(TadSubPacketCode::UnitBuildStarted));
                auto statePos = subPacketPosition(subs, static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove));
                REQUIRE(buildStartedPos >= 0);
                REQUIRE(statePos >= 0);
                REQUIRE(buildStartedPos < statePos);

                auto built = tadDecodeBuildStarted(subs[buildStartedPos]);
                REQUIRE(built);
                REQUIRE(built->typeIndex == 2);
                REQUIRE(built->unitId == 2);
                REQUIRE(built->position == TadPosition{simScalarToFixed(140_ss), 0, simScalarToFixed(200_ss)});

                // No 0x12 yet: the frame has only just been placed, and the
                // full-state record at tick 1 is the frame itself, showing
                // progress that is not complete.
                REQUIRE(subPacketPosition(subs, static_cast<uint8_t>(TadSubPacketCode::UnitBuildFinished)) < 0);
                auto state = tadDecodeUnitState(*firstSubPacketOf(subs, static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
                REQUIRE(state->sync->index == 1);
                REQUIRE(state->sync->typeIndex == 2);
                REQUIRE(state->sync->buildProgress > 0);
            }

            simulation.getUnitState(frame).buildTimeCompleted = simulation.unitDefinitions.at("ARMSOLAR").buildTime;
            simulation.gameTime += GameTime(1);
            recorder.endOfTick(simulation);
            recorder.close();

            {
                auto handler = readDemo(stream.str());
                // The first tick's packet and then the completing tick's, two
                // senders each.
                REQUIRE(handler.packets.size() == 4);
                auto finished = tadDecodeBuildFinished(*firstSubPacketOf(handler.subPackets[2], static_cast<uint8_t>(TadSubPacketCode::UnitBuildFinished)));
                REQUIRE(finished);
                REQUIRE(finished->unitId == 2);
                REQUIRE(finished->builderId == 1);
            }
        }

        SECTION("reuses a dead unit's slot in the demo's own blocks")
        {
            // RWE recycles UnitIds, so the recorder has to release a slot when
            // a unit leaves or a new unit inherits the old one's demo id.
            recorder.unitRemoved(armCommander);
            auto replacement = addUnitOfType(simulation, "ARMCOM", arm, SimVector(120_ss, 0_ss, 200_ss), makeEmptyCobScript());
            recorder.unitCreated(simulation, replacement);

            simulation.gameTime += GameTime(1);
            recorder.endOfTick(simulation);
            recorder.close();

            auto handler = readDemo(stream.str());
            auto state = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(state);
            REQUIRE(state->updates.size() == 1);
            REQUIRE(state->updates[0].index == 0);
            REQUIRE(state->updates[0].typeIndex == 1);
        }
    }
}
