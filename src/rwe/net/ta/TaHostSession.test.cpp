#include <catch2/catch_test_macros.hpp>
#include <rwe/net/ta/TaDirectPlay.h>
#include <rwe/net/ta/TaHostSession.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

/**
 * A joiner written in the test, doing the same side of the DirectPlay handshake
 * a real Total Annihilation does (docs/TA-NETWORK.md, "DirectPlay"), against the
 * host on ephemeral ports. It gets as far as CREATEPLAYER.
 */
namespace rwe
{
    namespace
    {
        using namespace std::chrono_literals;

        void putU16Le(std::vector<std::uint8_t>& out, std::size_t off, std::uint16_t value)
        {
            out[off] = static_cast<std::uint8_t>(value & 0xFF);
            out[off + 1] = static_cast<std::uint8_t>(value >> 8);
        }

        void putU16Be(std::vector<std::uint8_t>& out, std::size_t off, std::uint16_t value)
        {
            out[off] = static_cast<std::uint8_t>(value >> 8);
            out[off + 1] = static_cast<std::uint8_t>(value & 0xFF);
        }

        void putU32(std::vector<std::uint8_t>& out, std::size_t off, std::uint32_t value)
        {
            out[off] = static_cast<std::uint8_t>(value & 0xFF);
            out[off + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
            out[off + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
            out[off + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
        }

        void putAddress(std::vector<std::uint8_t>& out, std::size_t off, std::uint16_t port)
        {
            putU16Le(out, off, 2);
            putU16Be(out, off + 2, port);
            out[off + 4] = 127;
            out[off + 5] = 0;
            out[off + 6] = 0;
            out[off + 7] = 1;
        }

        void putUtf16(std::vector<std::uint8_t>& out, std::size_t off, const std::string& text)
        {
            auto bytes = taUtf8ToUtf16Le(text);
            std::copy(bytes.begin(), bytes.end(), out.begin() + static_cast<std::ptrdiff_t>(off));
        }

        std::vector<std::uint8_t> enumSessionsPayload()
        {
            std::vector<std::uint8_t> body(TaApplicationGuid.begin(), TaApplicationGuid.end());
            body.resize(body.size() + 8, 0);
            return body;
        }

        std::vector<std::uint8_t> addForwardRequestPayload(std::uint32_t systemPlayerId, std::uint16_t tcpPort, std::uint16_t udpPort, std::uint32_t targetId)
        {
            std::vector<std::uint8_t> body(106, 0);
            putU32(body, 4, systemPlayerId);
            putAddress(body, 68, tcpPort);
            putAddress(body, 84, udpPort);
            putU32(body, 102, targetId);
            return body;
        }

        std::vector<std::uint8_t> createPlayerPayload(std::uint32_t playerId, const std::string& name, std::uint16_t tcpPort, std::uint16_t udpPort)
        {
            std::vector<std::uint8_t> body(120, 0);
            putU32(body, 28, playerId);
            putUtf16(body, 68, name);
            putAddress(body, 88, tcpPort);
            putAddress(body, 104, udpPort);
            return body;
        }

        struct JoinerConnection
        {
            asio::ip::tcp::socket socket;
            std::array<std::uint8_t, 65536> scratch{};
            std::vector<std::uint8_t> pending;

            explicit JoinerConnection(asio::io_context& io) : socket(io) {}
        };

        class TestJoiner
        {
        public:
            TestJoiner(asio::io_context& ioContext, std::uint16_t hostGameTcp, std::uint16_t hostGameUdp)
                : ioContext(ioContext),
                  tcpListener(ioContext),
                  udpSocket(ioContext),
                  toHost(ioContext)
            {
                tcpListener.open(asio::ip::tcp::v4());
                tcpListener.set_option(asio::ip::tcp::acceptor::reuse_address(true));
                tcpListener.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
                tcpListener.listen();
                replyPort = tcpListener.local_endpoint().port();

                udpSocket.open(asio::ip::udp::v4());
                udpSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
                udpPort = udpSocket.local_endpoint().port();

                toHost.open(asio::ip::tcp::v4());
                toHost.connect(asio::ip::tcp::endpoint(asio::ip::make_address_v4("127.0.0.1"), hostGameTcp));
                hostUdpEndpoint = asio::ip::udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), hostGameUdp);

                acceptNext();
                listenUdp();
            }

            std::uint16_t tcpPort() const { return replyPort; }
            std::uint16_t udpPortValue() const { return udpPort; }

            void sendUdp(asio::ip::udp::endpoint endpoint, const std::vector<std::uint8_t>& message)
            {
                udpSocket.send_to(asio::buffer(message), endpoint);
            }

            void sendToHost(const std::vector<std::uint8_t>& message)
            {
                asio::write(toHost, asio::buffer(message));
            }

            void sendAppDataTcp(std::uint32_t to, const std::vector<std::uint8_t>& payload)
            {
                sendToHost(taEncodeAppDataTcp(taMakeReplyAddress(replyPort, {127, 0, 0, 1}), 0x08D90E74, to, payload));
            }

            void sendAppDataUdp(std::uint32_t to, const std::vector<std::uint8_t>& payload)
            {
                auto message = taEncodeAppDataUdp(0x08D90E74, to, payload);
                sendUdp(hostUdpEndpoint, message);
            }

            /** Every framed message the host has sent, in arrival order. */
            const std::vector<const std::vector<std::uint8_t>*>& received() const { return messages; }

            bool hasCommand(TaDirectPlayCommand command) const
            {
                for (const auto* message : messages)
                {
                    if (taIsDirectPlayMessage(message->data(), message->size()) && taDecodeDirectPlayMessage(message->data(), message->size()).command == command)
                    {
                        return true;
                    }
                }
                return false;
            }

            const std::vector<std::uint8_t>* findCommand(TaDirectPlayCommand command) const
            {
                for (const auto* message : messages)
                {
                    if (taIsDirectPlayMessage(message->data(), message->size()) && taDecodeDirectPlayMessage(message->data(), message->size()).command == command)
                    {
                        return message;
                    }
                }
                return nullptr;
            }

            std::vector<TaAppData> receivedAppDataTcp;
            std::vector<TaAppData> receivedAppDataUdp;

        private:
            asio::io_context& ioContext;
            asio::ip::tcp::acceptor tcpListener;
            asio::ip::udp::socket udpSocket;
            asio::ip::tcp::socket toHost;
            std::uint16_t replyPort{0};
            std::uint16_t udpPort{0};
            asio::ip::udp::endpoint hostUdpEndpoint;

            std::vector<std::shared_ptr<JoinerConnection>> connections;
            std::vector<std::shared_ptr<std::vector<std::uint8_t>>> owned;
            std::vector<const std::vector<std::uint8_t>*> messages;
            std::array<std::uint8_t, 65536> udpScratch{};

            void acceptNext()
            {
                auto connection = std::make_shared<JoinerConnection>(ioContext);
                tcpListener.async_accept(connection->socket, [this, connection](const asio::error_code& error) {
                    if (!error)
                    {
                        connections.push_back(connection);
                        readNext(connection);
                    }
                    acceptNext();
                });
            }

            void readNext(std::shared_ptr<JoinerConnection> connection)
            {
                connection->socket.async_read_some(
                    asio::buffer(connection->scratch.data(), connection->scratch.size()),
                    [this, connection](const asio::error_code& error, std::size_t bytes) {
                        if (error)
                        {
                            return;
                        }
                        connection->pending.insert(connection->pending.end(), connection->scratch.begin(), connection->scratch.begin() + static_cast<std::ptrdiff_t>(bytes));
                        handleBytes(connection->pending);
                        readNext(connection);
                    });
            }

            void handleBytes(std::vector<std::uint8_t>& buffer)
            {
                while (buffer.size() >= 4)
                {
                    auto size = taPeekDirectPlayMessageSize(buffer.data(), buffer.size());
                    if (size < 4 || size > buffer.size())
                    {
                        return;
                    }

                    auto ownedMessage = std::make_shared<std::vector<std::uint8_t>>(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
                    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));

                    if (taIsDirectPlayMessage(ownedMessage->data(), ownedMessage->size()))
                    {
                        messages.push_back(ownedMessage.get());
                    }
                    else
                    {
                        receivedAppDataTcp.push_back(taDecodeAppDataTcp(ownedMessage->data(), ownedMessage->size()));
                    }
                    owned.push_back(ownedMessage);
                }
            }

            void listenUdp()
            {
                auto sender = std::make_shared<asio::ip::udp::endpoint>();
                udpSocket.async_receive_from(
                    asio::buffer(udpScratch.data(), udpScratch.size()),
                    *sender,
                    [this, sender](const asio::error_code& error, std::size_t bytes) {
                        if (!error && !taIsDirectPlayMessage(udpScratch.data(), bytes))
                        {
                            receivedAppDataUdp.push_back(taDecodeAppDataUdp(udpScratch.data(), bytes));
                        }
                        listenUdp();
                    });
            }
        };

        void runUntil(asio::io_context& ioContext, const std::function<bool()>& done)
        {
            // Poll without sleeping. The wait here is for handlers to run, and
            // they run as fast as the machine will run them; sleeping between
            // them only spends the budget. It used to sleep 1 ms a handler
            // against a 5 s deadline, which a handshake of any length could
            // exhaust in a debug build on a loaded machine -- five cases in
            // this file failed there while the release build passed.
            //
            // The deadline is a backstop against a hang, not the test's
            // assertion: `done()` below is what says the work finished.
            auto deadline = std::chrono::steady_clock::now() + 60s;
            while (!done() && std::chrono::steady_clock::now() < deadline)
            {
                ioContext.poll_one();
            }
            REQUIRE(done());
        }

        /** Pump the io_context for a while, for the tests that wait a timeout out. */
        void runFor(asio::io_context& ioContext, std::chrono::milliseconds duration)
        {
            auto deadline = std::chrono::steady_clock::now() + duration;
            while (std::chrono::steady_clock::now() < deadline)
            {
                ioContext.poll_one();
                std::this_thread::sleep_for(1ms);
            }
        }

        /** A TCP connection to the host that says nothing, held open. */
        std::shared_ptr<asio::ip::tcp::socket> openSilentConnection(asio::io_context& ioContext, std::uint16_t port)
        {
            auto socket = std::make_shared<asio::ip::tcp::socket>(ioContext);
            socket->connect(asio::ip::tcp::endpoint(asio::ip::make_address_v4("127.0.0.1"), port));
            return socket;
        }
    }

    TEST_CASE("a joiner handshakes with the host and gets as far as CREATEPLAYER", "[ta][host]")
    {
        asio::io_context ioContext;

        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        config.gameName = "rwe";
        config.mapName = "Coast To Coast";

        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        std::vector<TaHostSession::PeerId> left;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });
        host.onPeerLeft([&](TaHostSession::PeerId id) { left.push_back(id); });

        REQUIRE(host.localPorts().enumSessions != 0);
        REQUIRE(host.localPorts().gameTcp != 0);
        REQUIRE(host.localPorts().gameUdp != 0);

        TestJoiner joiner(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);

        // ENUMSESSIONS by UDP, as a real joiner broadcasts it.
        auto enumRequest = taEncodeSystemMessage(
            TaDirectPlayCommand::EnumSessions,
            taMakeReplyAddress(joiner.tcpPort(), {127, 0, 0, 1}),
            enumSessionsPayload());
        joiner.sendUdp(
            asio::ip::udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), host.localPorts().enumSessions),
            enumRequest);

        runUntil(ioContext, [&] { return joiner.hasCommand(TaDirectPlayCommand::EnumSessionsReply); });

        // REQUESTPLAYERID, flags 9: a system player.
        joiner.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::RequestPlayerId,
            taMakeReplyAddress(joiner.tcpPort(), {127, 0, 0, 1}),
            taEncodeRequestPlayerId(9)));
        runUntil(ioContext, [&] { return joiner.hasCommand(TaDirectPlayCommand::RequestPlayerReply); });

        auto* systemReply = joiner.findCommand(TaDirectPlayCommand::RequestPlayerReply);
        auto systemPlayerId = taDecodeRequestPlayerReply(
            taDecodeDirectPlayMessage(systemReply->data(), systemReply->size()).payload);

        // ADDFORWARDREQUEST, carrying the joiner's own addresses.
        joiner.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::AddForwardRequest,
            taMakeReplyAddress(joiner.tcpPort(), {127, 0, 0, 1}),
            addForwardRequestPayload(systemPlayerId, joiner.tcpPort(), joiner.udpPortValue(), config.hostPlayerId)));
        runUntil(ioContext, [&] { return joiner.hasCommand(TaDirectPlayCommand::SuperEnumPlayersReply); });

        // REQUESTPLAYERID, flags 8: a named player.
        joiner.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::RequestPlayerId,
            taMakeReplyAddress(joiner.tcpPort(), {127, 0, 0, 1}),
            taEncodeRequestPlayerId(8)));
        auto playerId = 0x08D90E74u;

        // CREATEPLAYER: the host accepts and tells everyone.
        joiner.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::CreatePlayer,
            taMakeReplyAddress(joiner.tcpPort(), {127, 0, 0, 1}),
            createPlayerPayload(playerId, "joiner", joiner.tcpPort(), joiner.udpPortValue())));

        runUntil(ioContext, [&] { return joined.size() == 1 && joiner.hasCommand(TaDirectPlayCommand::SessionDescChanged); });
        REQUIRE(joined[0] == playerId);

        auto* changedMessage = joiner.findCommand(TaDirectPlayCommand::SessionDescChanged);
        auto changed = taDecodeSessionDescChanged(taDecodeDirectPlayMessage(changedMessage->data(), changedMessage->size()).payload);
        REQUIRE(changed.name == "rwe             Coast To Coast");
        REQUIRE(changed.session.currentPlayers == 2);

        // The host can now send application data to the peer, both ways.
        host.send(playerId, std::vector<std::uint8_t>{0xAA, 0xBB}, TaTransport::Tcp);
        runUntil(ioContext, [&] { return !joiner.receivedAppDataTcp.empty(); });
        REQUIRE(joiner.receivedAppDataTcp[0].from == config.hostPlayerId);
        REQUIRE(joiner.receivedAppDataTcp[0].to == playerId);
        REQUIRE(joiner.receivedAppDataTcp[0].payload == std::vector<std::uint8_t>{0xAA, 0xBB});

        host.send(playerId, std::vector<std::uint8_t>{0xCC}, TaTransport::Udp);
        runUntil(ioContext, [&] { return !joiner.receivedAppDataUdp.empty(); });
        REQUIRE(joiner.receivedAppDataUdp[0].from == config.hostPlayerId);
        REQUIRE(joiner.receivedAppDataUdp[0].payload == std::vector<std::uint8_t>{0xCC});

        // And it hands what the peer sends back to the application.
        std::vector<std::uint8_t> fromPeer;
        host.onAppData([&](TaHostSession::PeerId, const std::vector<std::uint8_t>& bytes, TaTransport) { fromPeer = bytes; });
        joiner.sendAppDataTcp(config.hostPlayerId, {0x11, 0x22, 0x33});
        runUntil(ioContext, [&] { return fromPeer == std::vector<std::uint8_t>{0x11, 0x22, 0x33}; });

        fromPeer.clear();
        joiner.sendAppDataUdp(config.hostPlayerId, {0x44});
        runUntil(ioContext, [&] { return fromPeer == std::vector<std::uint8_t>{0x44}; });

        // DELETEPLAYER is how a peer leaves, in that direction.
        joiner.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::DeletePlayer,
            taMakeReplyAddress(joiner.tcpPort(), {127, 0, 0, 1}),
            taEncodeDeletePlayer(playerId)));
        runUntil(ioContext, [&] { return left.size() == 1; });
        REQUIRE(left[0] == playerId);

        host.stop();
    }

    TEST_CASE("the host refuses to send to a peer it does not have", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        TaHostSession host(ioContext, config);
        host.start();

        REQUIRE_FALSE(host.send(0xDEADBEEF, std::vector<std::uint8_t>{0x01}, TaTransport::Tcp));
        REQUIRE_FALSE(host.send(0xDEADBEEF, std::vector<std::uint8_t>{0x01}, TaTransport::Udp));

        host.stop();
    }

    TEST_CASE("a flood of handshakes cannot grow the pending map for ever", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        config.pendingHandshakeTimeout = std::chrono::milliseconds(80);
        TaHostSession host(ioContext, config);
        host.start();

        TestJoiner joiner(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);

        SECTION("the map stops growing at the cap")
        {
            // The handshake key is the reply port in the header, which the
            // sender picks, so every one of these is a different key.
            for (std::uint16_t port = 20000; port < 20000 + TaHostSession::MaxPendingHandshakes * 3; ++port)
            {
                joiner.sendToHost(taEncodeSystemMessage(
                    TaDirectPlayCommand::RequestPlayerId,
                    taMakeReplyAddress(port, {127, 0, 0, 1}),
                    taEncodeRequestPlayerId(9)));
            }

            runUntil(ioContext, [&] { return host.load().pendingHandshakes == TaHostSession::MaxPendingHandshakes; });
            REQUIRE(host.load().pendingHandshakes <= TaHostSession::MaxPendingHandshakes);
        }

        SECTION("a handshake that never reaches CREATEPLAYER expires")
        {
            for (std::uint16_t port = 21000; port < 21003; ++port)
            {
                joiner.sendToHost(taEncodeSystemMessage(
                    TaDirectPlayCommand::RequestPlayerId,
                    taMakeReplyAddress(port, {127, 0, 0, 1}),
                    taEncodeRequestPlayerId(9)));
            }

            runUntil(ioContext, [&] { return host.load().pendingHandshakes == 3; });
            runFor(ioContext, std::chrono::milliseconds(600));
            REQUIRE(host.load().pendingHandshakes == 0);
        }

        host.stop();
    }

    TEST_CASE("the host refuses a joiner once the session is full", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });

        TestJoiner joiner(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);

        auto reply = taMakeReplyAddress(joiner.tcpPort(), {127, 0, 0, 1});
        joiner.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::RequestPlayerId, reply, taEncodeRequestPlayerId(9)));

        // The session advertises ten players and the host is one of them.
        auto roomFor = TaHostSession::MaxPlayers - 1;
        for (std::uint32_t i = 0; i < roomFor; ++i)
        {
            joiner.sendToHost(taEncodeSystemMessage(
                TaDirectPlayCommand::CreatePlayer,
                reply,
                createPlayerPayload(0x08D90E74u + i, "p" + std::to_string(i), joiner.tcpPort(), joiner.udpPortValue())));
        }

        runUntil(ioContext, [&] { return host.load().peers == roomFor; });
        REQUIRE(joined.size() == roomFor);

        // The next one is over the advertised maximum and is dropped.
        joiner.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::CreatePlayer,
            reply,
            createPlayerPayload(0x08D90EFF, "one too many", joiner.tcpPort(), joiner.udpPortValue())));

        runFor(ioContext, std::chrono::milliseconds(200));
        REQUIRE(joined.size() == roomFor);
        REQUIRE(host.load().peers == roomFor);

        host.stop();
    }

    TEST_CASE("accepted connections are capped", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        // Long enough that nothing times out while the queue fills. The cap and
        // the idle close are separate properties and one timeout cannot carry
        // both: at 120 ms this raced, because the connections accepted first
        // were closed again before the last of the cap had been accepted, and
        // the count never reached the cap on any machine slower than the test.
        config.incomingIdleTimeout = std::chrono::seconds(30);
        TaHostSession host(ioContext, config);
        host.start();

        // Each accepted socket holds a 64 KB read buffer, so the accept queue
        // is a cap on memory rather than a detail.
        std::vector<std::shared_ptr<asio::ip::tcp::socket>> silent;
        for (std::size_t i = 0; i < TaHostSession::MaxIncomingConnections + 8; ++i)
        {
            silent.push_back(openSilentConnection(ioContext, host.localPorts().gameTcp));
        }

        runUntil(ioContext, [&] { return host.load().incomingConnections == TaHostSession::MaxIncomingConnections; });
        REQUIRE(host.load().incomingConnections == TaHostSession::MaxIncomingConnections);

        host.stop();
    }

    TEST_CASE("a connection that never speaks is closed again", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        config.incomingIdleTimeout = std::chrono::milliseconds(120);
        TaHostSession host(ioContext, config);
        host.start();

        auto silent = openSilentConnection(ioContext, host.localPorts().gameTcp);
        runUntil(ioContext, [&] { return host.load().incomingConnections == 1; });
        REQUIRE(host.load().incomingConnections == 1);

        // None of them ever speaks, so every one of them is closed again.
        runFor(ioContext, std::chrono::milliseconds(600));
        REQUIRE(host.load().incomingConnections == 0);

        host.stop();
    }

    TEST_CASE("a reply goes to the packet's source, not to an address in the header", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        config.gameName = "rwe";
        TaHostSession host(ioContext, config);
        host.start();

        TestJoiner joiner(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);

        // The header names a third party. Honouring it would let anyone make
        // the host send to whoever they liked; only the port may be taken.
        joiner.sendUdp(
            asio::ip::udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), host.localPorts().enumSessions),
            taEncodeSystemMessage(
                TaDirectPlayCommand::EnumSessions,
                taMakeReplyAddress(joiner.tcpPort(), {10, 11, 12, 13}),
                enumSessionsPayload()));

        runUntil(ioContext, [&] { return joiner.hasCommand(TaDirectPlayCommand::EnumSessionsReply); });

        auto* reply = joiner.findCommand(TaDirectPlayCommand::EnumSessionsReply);
        auto decoded = taDecodeEnumSessionsReply(taDecodeDirectPlayMessage(reply->data(), reply->size()).payload);
        REQUIRE(decoded.name == "rwe             ");

        host.stop();
    }
}
