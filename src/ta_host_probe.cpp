// Hosts a DirectPlay game beside whatever else is on the machine and logs every
// message it receives, so a real Total Annihilation -- or a tool that speaks the
// same handshake -- can be pointed at it.
//
// With --battleroom it also plays the host's half of what a joining TA does
// between arriving and playing: TaBattleroom holds the battleroom, answers unit
// sync by echo and runs the launch, and this probe keeps the host in the game
// afterwards with a 0x2c for its one commander. The scripted joiner it is
// checked against is tools/ta-net/host-check.sh in the worktree of issue #430.
//
// The first check that the C++ session layer works against a real client is
// tools/ta-net/dpenum.py, which does not take a port base and so needs the real
// ports (47624, 2300, 2350). --port-base lets the probe run beside them, for a
// test that brings its own client.
//
// Usage: ta_host_probe --session-name x --map "Coast To Coast" [--port-base N]
//   --session-name  the game's name, padded to 16 in the session name
//   --map           the map name appended to it
//   --port-base     every port is the real one plus this; 0 (the default) is
//                   the real ports
//   --options       the host's options byte (docs/TA-NETWORK.md), default 0x4f
//   --battleroom    hold a battleroom, answer unit sync and launch
//   --auto-launch   with --battleroom, launch as soon as a joiner is ready
//   --team          the host's team in the battleroom, 5 (the default) for none
//   --max-units     the unit limit, which sets the 0x2c full-state cycle
//   --commander-unit  the host commander's unit id, 251 for block 1 slot 0

#include <asio.hpp>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/net/ta/TaBattleroom.h>
#include <rwe/net/ta/TaDirectPlay.h>
#include <rwe/net/ta/TaHostSession.h>
#include <rwe/net/ta/TaOutboundBatcher.h>
#include <rwe/net/ta/TaPacket.h>
#include <rwe/net/ta/TaPinger.h>
#include <rwe/util/OpaqueArgs.h>
#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace
{
    std::string hexByte(std::uint8_t value)
    {
        static const char* digits = "0123456789abcdef";
        std::string result{"0x"};
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0xF]);
        return result;
    }

    std::string hexU32(std::uint32_t value)
    {
        static const char* digits = "0123456789abcdef";
        std::string result;
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            result.push_back(digits[(value >> shift) & 0xF]);
        }
        return result;
    }

    std::string toString(rwe::TaTransport transport)
    {
        return transport == rwe::TaTransport::Tcp ? "TCP" : "UDP";
    }

    /** The clock a 0x02 carries: milliseconds, near GetTickCount. */
    std::uint32_t nowMs()
    {
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    /** How many 0x2c go in a message, and how often, as a real TA sends them. */
    constexpr int TicksPerPacket = 6;
    constexpr int TickRate = 30;

    /**
     * The host's one unit, as its owner describes it: a standing ARM commander
     * where the recorded host's own 0x09 put it (ta-baseline.pcap at 45.636),
     * with that data set's load-order index for the type. A probe has no data
     * set to read a commander's own figures from; a game supplies them.
     */
    rwe::TadUnitSync hostCommander()
    {
        rwe::TadUnitSync sync;
        sync.index = 0;
        sync.typeIndex = 34;
        sync.health = 3000;
        sync.buildProgress = 0;
        sync.flags10E = 4;
        sync.motionState = 1;
        sync.position = rwe::TadPosition{19922944, 5636096, 114294784};
        sync.rotation = rwe::TadRotation{0, 0, 0};
        return sync;
    }

    /**
     * The 0x2c stream that keeps a host in the game: each tick's full-state
     * record for the slot the tick names, so an empty slot is described as empty
     * and the commander's own is described once a cycle. The first message also
     * declares the commander as an entry, which is how a peer learns the slot is
     * occupied at all.
     */
    class HostInGame
    {
    public:
        HostInGame(
            asio::io_context& ioContext,
            rwe::TaOutboundBatcher& batcher,
            std::function<std::vector<rwe::TaHostSession::PeerId>()> everyone,
            std::uint16_t maxUnits,
            std::uint32_t commanderTypeIndex)
            : batcher(batcher),
              everyone(std::move(everyone)),
              layout(rwe::tadUnitStateLayout(std::vector<bool>(300, false), maxUnits)),
              commanderTypeIndex(commanderTypeIndex),
              timer(ioContext)
        {
        }

        void start()
        {
            arm();
        }

    private:
        rwe::TaOutboundBatcher& batcher;
        std::function<std::vector<rwe::TaHostSession::PeerId>()> everyone;
        rwe::TadUnitStateLayout layout;
        std::uint32_t commanderTypeIndex;
        asio::steady_timer timer;
        std::uint32_t tick{0};
        bool declared{false};

        void arm()
        {
            timer.expires_after(std::chrono::milliseconds(1000 * TicksPerPacket / TickRate));
            timer.async_wait([this](const asio::error_code& error) {
                if (error)
                {
                    return;
                }
                send();
                arm();
            });
        }

        void send()
        {
            auto ids = everyone();
            if (ids.empty())
            {
                return;
            }

            auto commander = hostCommander();
            for (int i = 0; i < TicksPerPacket; ++i)
            {
                ++tick;
                rwe::TadUnitState state;
                state.tick = tick;
                if (!declared)
                {
                    rwe::TadUnitUpdate update;
                    update.index = 0;
                    update.typeIndex = static_cast<std::uint16_t>(commanderTypeIndex);
                    update.mover = rwe::TadGroundPath{false, {}};
                    state.updates.push_back(update);
                }

                auto slot = static_cast<std::uint16_t>(tick % layout.maxUnits);
                rwe::TadUnitSync sync;
                sync.index = slot;
                if (slot == 0)
                {
                    sync = commander;
                }
                state.sync = sync;

                auto bytes = rwe::tadEncodeUnitState(state, layout);
                if (bytes.empty())
                {
                    std::cout << "0x2c for tick " << tick << " could not be written; stopping\n" << std::flush;
                    timer.cancel();
                    return;
                }
                batcher.queueForAll(ids, bytes, rwe::TaTransport::Udp);
            }
            declared = true;
            batcher.flush();
        }
    };
}

int main(int argc, char* argv[])
{
    rwe::OpaqueArgs args;

    try
    {
        args.parse(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        std::cerr << "usage: ta_host_probe --session-name x --map <map> [--port-base N]\n";
        return 1;
    }

    if (args.isHelpRequested() || !args.contains("session-name") || !args.contains("map"))
    {
        std::cout << "usage: ta_host_probe --session-name x --map <map> [--port-base N] [--options 0x4f]\n"
                     "                          [--battleroom] [--auto-launch]\n"
                  << "\n"
                  << "  --session-name  the game's name, padded to 16 in the session name\n"
                  << "  --map           the map name appended to it\n"
                  << "  --port-base     every port is the real one plus this (default 0)\n"
                  << "  --options       the host's options byte (default 0x4f)\n"
                  << "  --battleroom    hold a battleroom, echo unit sync and launch\n"
                  << "  --auto-launch   with --battleroom, launch as soon as a joiner is ready\n"
                  << "  --team          the host's team in the battleroom, 5 for none (default)\n"
                  << "  --max-units     the unit limit, which sets the 0x2c cycle (default 250)\n"
                  << "  --commander-unit  the host commander's unit id (default 251)\n"
                  << "\n"
                  << "Hosts on TCP and UDP 47624, 2300 and 2350 and logs what it receives.\n";
        return args.isHelpRequested() ? 0 : 1;
    }

    rwe::TaHostConfig config;
    config.gameName = args.getString("session-name");
    config.mapName = args.getString("map");
    config.ports = rwe::TaHostPorts::atBase(static_cast<int>(args.getUint("port-base", 0)));
    config.options = static_cast<std::uint8_t>(args.getUint("options", 0x4f));

    asio::io_context ioContext;
    rwe::TaHostSession host(ioContext, config);

    try
    {
        host.start();
    }
    catch (const std::exception& e)
    {
        std::cerr << "could not bind the DirectPlay ports: " << e.what() << "\n";
        return 1;
    }

    // A 0x02 is 20 bytes on the wire and does not compress, which is how the
    // captures carry it; everything else the host sends is compressed, which is
    // what the battleroom's own traffic and the in-game 0x2c are.
    bool const battleroomMode = args.getBool("battleroom");
    rwe::TaOutboundBatcher batcher(
        [&](rwe::TaHostSession::PeerId id, std::span<const std::uint8_t> bytes, rwe::TaTransport transport) {
            host.send(id, bytes, transport);
        },
        battleroomMode ? rwe::TadPacketCompressed : rwe::TadPacketUncompressed);

    std::optional<rwe::TaPinger> pinger;
    if (!battleroomMode)
    {
        pinger.emplace(batcher, config.hostPlayerId, nowMs);
    }

    std::vector<rwe::TaHostSession::PeerId> peers;
    std::optional<std::uint32_t> lastRoundTrip;

    rwe::TaBattleroomConfig roomConfig;
    roomConfig.mapName = config.mapName;
    roomConfig.options = config.options;
    roomConfig.hostTeam = static_cast<std::uint8_t>(args.getUint("team", rwe::TaNoTeam));
    std::optional<rwe::TaBattleroom> room;
    std::optional<HostInGame> inGame;
    bool const autoLaunch = args.getBool("auto-launch");
    std::uint16_t const maxUnits = static_cast<std::uint16_t>(args.getUint("max-units", 250));
    std::uint32_t const commanderUnitId = args.getUint("commander-unit", 251);
    constexpr std::uint16_t CommanderTypeIndex = 34;

    if (battleroomMode)
    {
        room.emplace(ioContext, host, batcher, nowMs, roomConfig);
        room->onReady([&](const rwe::TaBattleroomPeer& peer) {
            std::cout << "peer 0x" << std::hex << peer.playerId << std::dec
                      << " is ready on team " << static_cast<int>(peer.team) << "\n"
                      << std::flush;
            if (autoLaunch && room->state() == rwe::TaBattleroomState::Waiting)
            {
                rwe::TaBattleroom::LaunchParams params;
                params.commanderTypeIndex = CommanderTypeIndex;
                params.commanderUnitId = static_cast<std::uint16_t>(commanderUnitId);
                params.commanderPosition = hostCommander().position;
                std::cout << "launching: " << (room->launch(params) ? "yes" : "refused") << "\n" << std::flush;
            }
        });
        room->onRefused([&](const std::string& reason) {
            std::cout << "launch refused: " << reason << "\n" << std::flush;
        });
        room->onLaunched([&](const rwe::TaBattleroom::JoinerInfo& info) {
            std::cout << "launched: '" << info.name << "' 0x" << std::hex << info.address.playerId << std::dec
                      << " on tcp " << info.address.tcp.port() << ", udp " << info.address.udp.port()
                      << ", team " << static_cast<int>(info.team) << "\n"
                      << std::flush;
            inGame.emplace(
                ioContext,
                batcher,
                [&peers] { return peers; },
                maxUnits,
                CommanderTypeIndex);
            inGame->start();
        });
    }

    host.onPeerJoined([&](rwe::TaHostSession::PeerId id) {
        std::cout << "peer joined: 0x" << std::hex << id << std::dec << "\n" << std::flush;
        peers.push_back(id);
        if (room)
        {
            room->peerJoined(id);
        }
    });
    host.onPeerLeft([&](rwe::TaHostSession::PeerId id) {
        std::cout << "peer left: 0x" << std::hex << id << std::dec << "\n" << std::flush;
        peers.erase(std::remove(peers.begin(), peers.end(), id), peers.end());
        if (room)
        {
            room->peerLeft(id);
        }
    });
    host.onAppData([&](rwe::TaHostSession::PeerId id, const std::vector<std::uint8_t>& bytes, rwe::TaTransport transport) {
        if (room)
        {
            // The battleroom reads and answers the subpackets itself.
            room->handleAppData(id, bytes, transport);
            if (auto roundTrip = room->roundTripTicks(id); roundTrip && lastRoundTrip != *roundTrip)
            {
                // A joiner pings once a second and the log is read after a run,
                // so only a latency that has moved is worth a line.
                lastRoundTrip = *roundTrip;
                std::cout << "  ping 0x" << std::hex << id << std::dec << ": "
                          << *roundTrip << " ms\n" << std::flush;
            }
            return;
        }

        auto parsed = rwe::taParsePacket(bytes);
        if (!parsed)
        {
            std::cout << "app data from 0x" << std::hex << id << std::dec
                      << " over " << toString(transport) << ": " << bytes.size()
                      << " bytes, not a TA packet\n"
                      << std::flush;
            return;
        }

        std::cout << "app data from 0x" << std::hex << id << std::dec
                  << " over " << toString(transport) << ": " << bytes.size()
                  << " bytes, marker " << hexU32(parsed->packet.marker)
                  << (parsed->checksumValid ? "" : ", bad checksum")
                  << (parsed->stats.total() ? ", walk incomplete" : "") << "\n";
        for (const auto& subpacket : parsed->packet.subpackets)
        {
            std::cout << "  " << hexByte(subpacket[0]) << " " << subpacket.size() << " bytes\n";
            if (subpacket[0] == static_cast<std::uint8_t>(rwe::TadSubPacketCode::Ping))
            {
                pinger->handle(id, subpacket, transport);
            }
        }
        std::cout << std::flush;
        batcher.flush();
    });

    // A host that stops answering is offered for rejection, so the probe keeps
    // pinging and keeps the replies coming whichever side sends them. The
    // battleroom runs its own pings, on the clock it was given.
    asio::steady_timer pingTimer(ioContext);
    std::function<void()> pingInTwoSeconds = [&] {
        if (room)
        {
            return;
        }
        pingTimer.expires_after(std::chrono::seconds(2));
        pingTimer.async_wait([&](const asio::error_code& error) {
            if (error)
            {
                return;
            }
            if (!peers.empty())
            {
                pinger->sendRequests(peers);
                batcher.flush();
                for (rwe::TaHostSession::PeerId id : peers)
                {
                    if (auto roundTrip = pinger->roundTripTicks(id))
                    {
                        std::cout << "  ping 0x" << std::hex << id << std::dec << ": "
                                  << *roundTrip << " ms\n" << std::flush;
                    }
                }
            }
            pingInTwoSeconds();
        });
    };
    pingInTwoSeconds();

    std::cout << "hosting '" << rwe::taSessionName(config.gameName, config.mapName) << "'\n"
              << "  enum TCP/UDP " << host.localPorts().enumSessions << "\n"
              << "  game TCP     " << host.localPorts().gameTcp << "\n"
              << "  game UDP     " << host.localPorts().gameUdp << "\n"
              << std::flush;

    asio::signal_set signals(ioContext, SIGINT, SIGTERM);
    signals.async_wait([&](const asio::error_code&, int) {
        host.stop();
        ioContext.stop();
    });

    ioContext.run();
    return 0;
}
