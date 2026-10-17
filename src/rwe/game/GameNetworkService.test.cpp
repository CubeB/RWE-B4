#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <rwe/game/GameNetworkService.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/net/EndpointFamily.h>
#include <thread>

namespace rwe
{
    namespace
    {
        using asio::ip::udp;

        udp::endpoint at(const char* ip, unsigned short port)
        {
            return toDualStackEndpoint(udp::endpoint(asio::ip::make_address_v4(ip), port));
        }

        template <typename Pred>
        bool waitUntil(Pred pred, std::chrono::milliseconds limit = std::chrono::milliseconds(4000))
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (pred())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return pred();
        }

        /** Whether `service` has heard anything from its one peer. */
        bool hasHeardAPeer(GameNetworkService& service)
        {
            const auto peers = service.getPeerStatuses();
            return !peers.empty() && peers.front().lastKnownSceneTime.has_value();
        }
    }

    TEST_CASE("Any peer can resume shared pause without simulation commands", "[pause]")
    {
        PlayerCommandService commandsA;
        PlayerCommandService commandsB;
        GameNetworkService a(PlayerId(0), 1431, {GameNetworkService::EndpointInfo(PlayerId(1), at("127.0.0.1", 1432))}, &commandsA);
        GameNetworkService b(PlayerId(1), 1432, {GameNetworkService::EndpointInfo(PlayerId(0), at("127.0.0.1", 1431))}, &commandsB);
        a.start();
        b.start();
        REQUIRE(a.togglePause().paused);
        REQUIRE(waitUntil([&]() { return b.sharedPauseState().paused; }));
        REQUIRE_FALSE(b.togglePause().paused);
        REQUIRE(waitUntil([&]() { return !a.sharedPauseState().paused; }));
        REQUIRE(b.togglePause().paused);
        REQUIRE(waitUntil([&]() { return a.sharedPauseState().paused; }));
        REQUIRE_FALSE(a.togglePause().paused);
        REQUIRE(waitUntil([&]() { return !b.sharedPauseState().paused; }));
        REQUIRE(a.sharedPauseState().revision == 4);
        REQUIRE(b.sharedPauseState().revision == 4);

        // A returning link must inherit changes made while it was absent.
        // Otherwise it can keep advertising its old pause indefinitely.
        a.forgetPeer(PlayerId(1));
        REQUIRE(a.getPeerStatuses().empty());
        REQUIRE(a.togglePause().paused);
        REQUIRE(a.rememberPeer(PlayerId(1), SequenceNumber(0), SequenceNumber(0)));
        REQUIRE(waitUntil([&]() { return b.sharedPauseState().paused; }));
        REQUIRE_FALSE(b.togglePause().paused);
        REQUIRE(waitUntil([&]() { return !a.sharedPauseState().paused; }));
    }

    TEST_CASE("GameNetworkService follows a peer that is sending from somewhere it was not told")
    {
        // The lobby's address for a peer behind a NAT is a guess at the port.
        // The first packet that arrives from the right machine on another
        // port is how the guess is corrected -- if it says who it is.
        constexpr unsigned short portA = 1411;
        constexpr unsigned short portB = 1412;
        constexpr unsigned short wrongPort = 1499;

        PlayerCommandService commandsA;
        commandsA.registerPlayer(PlayerId(0));
        commandsA.registerPlayer(PlayerId(1));
        PlayerCommandService commandsB;
        commandsB.registerPlayer(PlayerId(0));
        commandsB.registerPlayer(PlayerId(1));

        SECTION("the right seat from the right address on another port is followed")
        {
            GameNetworkService a(PlayerId(0), portA, {GameNetworkService::EndpointInfo(PlayerId(1), at("127.0.0.1", wrongPort))}, &commandsA);
            GameNetworkService b(PlayerId(1), portB, {GameNetworkService::EndpointInfo(PlayerId(0), at("127.0.0.1", portA))}, &commandsB);
            a.start();
            b.start();

            b.submitCommands(SceneTime(1), {});
            REQUIRE(waitUntil([&]() {
                b.submitCommands(SceneTime(1), {});
                return hasHeardAPeer(a);
            }));
        }

        SECTION("a peer at the address it was told needs no following")
        {
            GameNetworkService a(PlayerId(0), portA, {GameNetworkService::EndpointInfo(PlayerId(1), at("127.0.0.1", portB))}, &commandsA);
            GameNetworkService b(PlayerId(1), portB, {GameNetworkService::EndpointInfo(PlayerId(0), at("127.0.0.1", portA))}, &commandsB);
            a.start();
            b.start();

            REQUIRE(waitUntil([&]() {
                b.submitCommands(SceneTime(1), {});
                return hasHeardAPeer(a);
            }));
        }

        SECTION("a different address is not followed, whatever it says")
        {
            // The peer was given as 192.0.2.7; the packets come from
            // 127.0.0.1, which is not the same carrier block.
            GameNetworkService a(PlayerId(0), portA, {GameNetworkService::EndpointInfo(PlayerId(1), at("192.0.2.7", wrongPort))}, &commandsA);
            GameNetworkService b(PlayerId(1), portB, {GameNetworkService::EndpointInfo(PlayerId(0), at("127.0.0.1", portA))}, &commandsB);
            a.start();
            b.start();

            for (int i = 0; i < 20; ++i)
            {
                b.submitCommands(SceneTime(1), {});
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            CHECK_FALSE(hasHeardAPeer(a));
        }

        SECTION("a neighbouring address of the same carrier block is followed")
        {
            // Given as 127.0.0.2, heard from 127.0.0.1: one machine's two
            // sockets on a carrier's NAT leave from two neighbouring addresses.
            GameNetworkService a(PlayerId(0), portA, {GameNetworkService::EndpointInfo(PlayerId(1), at("127.0.0.2", wrongPort))}, &commandsA);
            GameNetworkService b(PlayerId(1), portB, {GameNetworkService::EndpointInfo(PlayerId(0), at("127.0.0.1", portA))}, &commandsB);
            a.start();
            b.start();

            REQUIRE(waitUntil([&]() {
                b.submitCommands(SceneTime(1), {});
                return hasHeardAPeer(a);
            }));
        }

        SECTION("a seat that is not the one expected from that address is not followed")
        {
            // B says it is player 2; A has no player 2 at that address.
            GameNetworkService a(PlayerId(0), portA, {GameNetworkService::EndpointInfo(PlayerId(1), at("127.0.0.1", wrongPort))}, &commandsA);
            commandsB.registerPlayer(PlayerId(2));
            GameNetworkService b(PlayerId(2), portB, {GameNetworkService::EndpointInfo(PlayerId(0), at("127.0.0.1", portA))}, &commandsB);
            a.start();
            b.start();

            for (int i = 0; i < 20; ++i)
            {
                b.submitCommands(SceneTime(1), {});
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            CHECK_FALSE(hasHeardAPeer(a));
        }
    }

    TEST_CASE("a chat line addressed to some peers reaches only those peers (BT-176)")
    {
        // Three machines on loopback. Player 0 says a line to player 1 alone
        // and then one to everybody; player 2 must see only the second.
        PlayerCommandService commandsA;
        PlayerCommandService commandsB;
        PlayerCommandService commandsC;
        for (auto* c : {&commandsA, &commandsB, &commandsC})
        {
            c->registerPlayer(PlayerId(0));
            c->registerPlayer(PlayerId(1));
            c->registerPlayer(PlayerId(2));
        }

        GameNetworkService a(PlayerId(0), 1441, {GameNetworkService::EndpointInfo(PlayerId(1), at("127.0.0.1", 1442)), GameNetworkService::EndpointInfo(PlayerId(2), at("127.0.0.1", 1443))}, &commandsA);
        GameNetworkService b(PlayerId(1), 1442, {GameNetworkService::EndpointInfo(PlayerId(0), at("127.0.0.1", 1441)), GameNetworkService::EndpointInfo(PlayerId(2), at("127.0.0.1", 1443))}, &commandsB);
        GameNetworkService c(PlayerId(2), 1443, {GameNetworkService::EndpointInfo(PlayerId(0), at("127.0.0.1", 1441)), GameNetworkService::EndpointInfo(PlayerId(1), at("127.0.0.1", 1442))}, &commandsC);
        a.start();
        b.start();
        c.start();

        auto ids = a.remotePlayerIds();
        REQUIRE(ids.size() == 2);

        REQUIRE(a.submitChatMessage("only for you", ChatLabel::Allies, {PlayerId(1)}));

        std::vector<GameNetworkService::ReceivedChatMessage> atB;
        REQUIRE(waitUntil([&]() {
            for (auto& m : b.takeChatMessages())
            {
                atB.push_back(m);
            }
            return !atB.empty();
        }));
        REQUIRE(atB.front().text == "only for you");
        REQUIRE(atB.front().label == ChatLabel::Allies);
        REQUIRE(atB.front().sender == PlayerId(0));

        REQUIRE(a.submitChatMessage("hello everyone", ChatLabel::None, {PlayerId(1), PlayerId(2)}));

        std::vector<GameNetworkService::ReceivedChatMessage> atC;
        REQUIRE(waitUntil([&]() {
            for (auto& m : c.takeChatMessages())
            {
                atC.push_back(m);
            }
            return !atC.empty();
        }));

        // The first line was never put on the wire to player 2, so the only
        // thing it ever heard is the public one.
        REQUIRE(atC.size() == 1);
        REQUIRE(atC.front().text == "hello everyone");
        REQUIRE(atC.front().label == ChatLabel::None);
    }
}
