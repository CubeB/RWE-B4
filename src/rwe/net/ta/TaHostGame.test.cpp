#include <catch2/catch_test_macros.hpp>
#include <rwe/net/ta/TaHostGame.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

/**
 * The game a joining TA is put into, on its own thread. What there is to check
 * here is the join between the two threads: the host protocol itself is checked
 * in TaHostSession.test.cpp and TaBattleroom.test.cpp, and host-check.sh checks
 * the whole of it end to end.
 */
namespace rwe
{
    namespace
    {
        using namespace std::chrono_literals;

        /** Waits for a thing to become true, up to a second and a half. */
        template <typename Predicate>
        bool eventually(Predicate predicate)
        {
            for (int i = 0; i < 150; ++i)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(10ms);
            }
            return predicate();
        }

        /** A host on ports the OS chooses, which is what a test can be sure of. */
        TaHostGameConfig ephemeralConfig()
        {
            TaHostGameConfig config;
            config.mapName = "test";
            config.ports = TaHostPorts{0, 0, 0};
            return config;
        }
    }

    TEST_CASE("a host binds its three ports and says which they are", "[ta][host]")
    {
        TaHostGame host{ephemeralConfig()};
        auto lobby = host.lobbyState();

        REQUIRE(lobby.enumPort != 0);
        REQUIRE(lobby.gameTcpPort != 0);
        REQUIRE(lobby.gameUdpPort != 0);
        REQUIRE(lobby.phase == TaBattleroomState::Waiting);
        REQUIRE(lobby.peers.empty());
        REQUIRE_FALSE(lobby.launched);
    }

    TEST_CASE("a host's commander is block one, slot zero", "[ta][host]")
    {
        auto config = ephemeralConfig();
        config.maxUnits = 250;
        TaHostGame host{config};

        // A joining TA takes block 0 for its own units, and 250 to a block is
        // what the recorded host and joiner were on.
        REQUIRE(host.commanderUnitId() == 251);

        config.maxUnits = 1000;
        TaHostGame other{config};
        REQUIRE(other.commanderUnitId() == 1001);
    }

    TEST_CASE("a host with no peer has nothing for the game and refuses nothing", "[ta][host]")
    {
        TaHostGame host{ephemeralConfig()};

        REQUIRE(host.takeInbound().empty());

        // A message for a peer the host does not have costs the message and
        // nothing else, and neither call may block: both are on the game
        // thread's side of the queues.
        const std::vector<std::uint8_t> bytes{0x0c, 0x00, 0x00};
        host.queueOutbound(0x1234, bytes, TaTransport::Udp);
        REQUIRE(eventually([&]() { return host.lobbyState().phase == TaBattleroomState::Waiting; }));
        REQUIRE(host.stats().outboundDropped == 0);
        REQUIRE(host.takeInbound().empty());

        // A launch with nobody in the battleroom is not a refusal, and it is
        // given as often as it is asked for.
        TaBattleroom::LaunchParams params;
        params.commanderTypeIndex = 34;
        params.commanderUnitId = host.commanderUnitId();
        host.launch(params);
        host.launch(params);
        REQUIRE(host.lobbyState().refusal.empty());
    }

    TEST_CASE("leaving is something a host can be told twice", "[ta][host]")
    {
        auto host = std::make_shared<TaHostGame>(ephemeralConfig());

        // Before a launch there is no game to quit out of, and the thread stops
        // either way. The second call has to find it already gone.
        host->leave();
        host->leave();
        REQUIRE(host->takeInbound().empty());
    }
}
