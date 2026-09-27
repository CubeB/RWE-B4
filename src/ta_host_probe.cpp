// Hosts a DirectPlay game beside whatever else is on the machine and logs every
// message it receives, so a real Total Annihilation -- or a tool that speaks the
// same handshake -- can be pointed at it.
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

#include <asio.hpp>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <rwe/net/ta/TaDirectPlay.h>
#include <rwe/net/ta/TaHostSession.h>
#include <rwe/util/OpaqueArgs.h>
#include <string>

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

    std::string toString(rwe::TaTransport transport)
    {
        return transport == rwe::TaTransport::Tcp ? "TCP" : "UDP";
    }
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
                  << "\n"
                  << "  --session-name  the game's name, padded to 16 in the session name\n"
                  << "  --map           the map name appended to it\n"
                  << "  --port-base     every port is the real one plus this (default 0)\n"
                  << "  --options       the host's options byte (default 0x4f)\n"
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

    host.onPeerJoined([](rwe::TaHostSession::PeerId id) {
        std::cout << "peer joined: 0x" << std::hex << id << std::dec << "\n" << std::flush;
    });
    host.onPeerLeft([](rwe::TaHostSession::PeerId id) {
        std::cout << "peer left: 0x" << std::hex << id << std::dec << "\n" << std::flush;
    });
    host.onAppData([](rwe::TaHostSession::PeerId id, const std::vector<std::uint8_t>& bytes, rwe::TaTransport transport) {
        std::cout << "app data from 0x" << std::hex << id << std::dec
                  << " over " << toString(transport) << ": " << bytes.size() << " bytes\n"
                  << std::flush;
    });

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
