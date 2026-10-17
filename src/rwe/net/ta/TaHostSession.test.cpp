#include <catch2/catch_test_macros.hpp>
#include <rwe/net/ta/TaDirectPlay.h>
#include <rwe/net/ta/TaHostSession.h>
#include <rwe/net/ta/TaPacket.h>

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
            /**
             * `sourceAddress` is the loopback address this joiner's packets come
             * from. Everything in 127.0.0.0/8 is this machine, so a second
             * joiner on 127.0.0.2 is a second source to the host, which is what
             * a forgery test needs.
             */
            TestJoiner(
                asio::io_context& ioContext,
                std::uint16_t hostGameTcp,
                std::uint16_t hostGameUdp,
                const std::string& sourceAddress = "127.0.0.1")
                : ioContext(ioContext),
                  tcpListener(ioContext),
                  udpSocket(ioContext),
                  toHost(ioContext)
            {
                auto source = asio::ip::make_address_v4(sourceAddress);

                // Listens on every address so the host's connection back to
                // whichever source it saw reaches it.
                tcpListener.open(asio::ip::tcp::v4());
                tcpListener.set_option(asio::ip::tcp::acceptor::reuse_address(true));
                tcpListener.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
                tcpListener.listen();
                replyPort = tcpListener.local_endpoint().port();

                udpSocket.open(asio::ip::udp::v4());
                udpSocket.bind(asio::ip::udp::endpoint(source, 0));
                udpPort = udpSocket.local_endpoint().port();

                toHost.open(asio::ip::tcp::v4());
                toHost.bind(asio::ip::tcp::endpoint(source, 0));
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

            void sendAppDataTcp(std::uint32_t to, const std::vector<std::uint8_t>& payload, std::uint32_t from = 0x08D90E74)
            {
                sendToHost(taEncodeAppDataTcp(taMakeReplyAddress(replyPort, {127, 0, 0, 1}), from, to, payload));
            }

            void sendAppDataUdp(std::uint32_t to, const std::vector<std::uint8_t>& payload, std::uint32_t from = 0x08D90E74)
            {
                auto message = taEncodeAppDataUdp(from, to, payload);
                sendUdp(hostUdpEndpoint, message);
            }

            /** CREATEPLAYER for `playerId`, as the handshake's last step. */
            void createPlayer(std::uint32_t playerId, const std::string& name)
            {
                sendToHost(taEncodeSystemMessage(
                    TaDirectPlayCommand::CreatePlayer,
                    taMakeReplyAddress(replyPort, {127, 0, 0, 1}),
                    createPlayerPayload(playerId, name, replyPort, udpPort)));
            }

            /** DELETEPLAYER for `playerId`, which only the player's own address may send. */
            void deletePlayer(std::uint32_t playerId)
            {
                sendToHost(taEncodeSystemMessage(
                    TaDirectPlayCommand::DeletePlayer,
                    taMakeReplyAddress(replyPort, {127, 0, 0, 1}),
                    taEncodeDeletePlayer(playerId)));
            }

            /** The reject reasons the host has sent this joiner, as 0x1b subpackets. */
            std::vector<TadBytes> rejects() const
            {
                std::vector<TadBytes> found;
                for (const auto& app : receivedAppDataTcp)
                {
                    auto parsed = taParsePacket(app.payload);
                    if (!parsed || !parsed->checksumValid)
                    {
                        continue;
                    }
                    for (const auto& subpacket : parsed->packet.subpackets)
                    {
                        if (!subpacket.empty() && subpacket[0] == 0x1b)
                        {
                            found.push_back(subpacket);
                        }
                    }
                }
                return found;
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
            addForwardRequestPayload(systemPlayerId, joiner.tcpPort(), joiner.udpPortValue(), config.sessionHostId)));
        runUntil(ioContext, [&] { return joiner.hasCommand(TaDirectPlayCommand::SuperEnumPlayersReply); });

        // What a real TA joiner needs before it sends CREATEPLAYER, pinned to
        // the real host in ta-baseline.pcap: without the name-server bit and
        // the host player's own id and data, a real TA sat in the battleroom
        // without ever announcing its player.
        {
            auto* superMessage = joiner.findCommand(TaDirectPlayCommand::SuperEnumPlayersReply);
            auto reply = taDecodeSuperEnumPlayersReply(taDecodeDirectPlayMessage(superMessage->data(), superMessage->size()).payload);
            REQUIRE(reply.session.hostPlayerId == 0x08D80E77u);
            REQUIRE(reply.session.user3 == 0x47010001u);
            REQUIRE(reply.session.instanceGuid == config.instanceGuid);
            REQUIRE((reply.session.flags & TaSessionJoinDisabled) == 0);

            auto hostSystem = std::find_if(reply.players.begin(), reply.players.end(), [](const auto& p) { return p.id == 0x08D90E77u; });
            REQUIRE(hostSystem != reply.players.end());
            REQUIRE(hostSystem->flags == 0x0Fu);

            auto hostPlayer = std::find_if(reply.players.begin(), reply.players.end(), [](const auto& p) { return p.id == 0x08D90E76u; });
            REQUIRE(hostPlayer != reply.players.end());
            REQUIRE(hostPlayer->flags == 0x0Cu);
            REQUIRE(hostPlayer->versionOrSystemPlayerId == 0x08D90E77u);
            REQUIRE(hostPlayer->playerData.size() == 21u);
            REQUIRE(hostPlayer->playerData[19] == 0x50);

            auto joinerSystem = std::find_if(reply.players.begin(), reply.players.end(), [&](const auto& p) { return p.id == systemPlayerId; });
            REQUIRE(joinerSystem != reply.players.end());
            REQUIRE(joinerSystem->flags == 0x05u);
        }

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
                // Down in pairs, as the host hands them out; counting up would
                // run into the host's own ids, which a joiner may not take.
                createPlayerPayload(0x08D90E74u - 2 * i, "p" + std::to_string(i), joiner.tcpPort(), joiner.udpPortValue())));
        }

        runUntil(ioContext, [&] { return host.load().peers == roomFor; });
        REQUIRE(joined.size() == roomFor);

        // The next one is over the advertised maximum and is turned away --
        // and told so, since a joiner left waiting is the worse outcome (#718).
        joiner.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::CreatePlayer,
            reply,
            createPlayerPayload(0x08D90EFF, "toomany", joiner.tcpPort(), joiner.udpPortValue())));

        runUntil(ioContext, [&] { return !joiner.rejects().empty(); });
        REQUIRE(joined.size() == roomFor);
        REQUIRE(host.load().peers == roomFor);
        REQUIRE(joiner.rejects().size() == 1);
        REQUIRE(joiner.rejects()[0] == taBuildReject(0x08D90EFF, TaRejectClosed));
        REQUIRE(host.load().pendingHandshakes == 0);

        host.stop();
    }

    TEST_CASE("a game that plays one peer takes one joiner and tells the second why not (#721)", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        config.maxJoiners = 1;
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });

        TestJoiner first(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);
        TestJoiner second(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp, "127.0.0.2");

        first.createPlayer(0x08D90E74, "first");
        runUntil(ioContext, [&] { return joined.size() == 1; });

        second.createPlayer(0x08D90E72, "second");
        runUntil(ioContext, [&] { return !second.rejects().empty(); });

        REQUIRE(joined.size() == 1);
        REQUIRE(host.load().peers == 1);
        REQUIRE(second.rejects()[0] == taBuildReject(0x08D90E72, TaRejectClosed));
        REQUIRE(first.rejects().empty());

        host.stop();
    }

    TEST_CASE("a peer is removed only by a DELETEPLAYER from the address it joined from (#715)", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        std::vector<TaHostSession::PeerId> left;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });
        host.onPeerLeft([&](TaHostSession::PeerId id) { left.push_back(id); });

        TestJoiner player(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);
        TestJoiner stranger(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp, "127.0.0.2");

        const std::uint32_t playerId = 0x08D90E74;
        player.createPlayer(playerId, "player");
        runUntil(ioContext, [&] { return joined.size() == 1; });

        // The ids are handed out in a fixed order, so anyone can name one.
        stranger.deletePlayer(playerId);
        runUntil(ioContext, [&] { return host.forgedMessages() == 1; });
        runFor(ioContext, std::chrono::milliseconds(100));
        REQUIRE(left.empty());
        REQUIRE(host.load().peers == 1);

        // The player's own address is believed.
        player.deletePlayer(playerId);
        runUntil(ioContext, [&] { return left.size() == 1; });
        REQUIRE(left[0] == playerId);
        REQUIRE(host.load().peers == 0);

        host.stop();
    }

    TEST_CASE("a DELETEPLAYER by the system player's id removes its peer, from its own address only", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        std::vector<TaHostSession::PeerId> left;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });
        host.onPeerLeft([&](TaHostSession::PeerId id) { left.push_back(id); });

        TestJoiner player(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);
        TestJoiner stranger(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp, "127.0.0.2");

        // The system player's id comes from the host, as a real joiner's does.
        player.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::RequestPlayerId,
            taMakeReplyAddress(player.tcpPort(), {127, 0, 0, 1}),
            taEncodeRequestPlayerId(9)));
        runUntil(ioContext, [&] { return player.hasCommand(TaDirectPlayCommand::RequestPlayerReply); });
        auto* systemReply = player.findCommand(TaDirectPlayCommand::RequestPlayerReply);
        auto systemPlayerId = taDecodeRequestPlayerReply(
            taDecodeDirectPlayMessage(systemReply->data(), systemReply->size()).payload);
        player.sendToHost(taEncodeSystemMessage(
            TaDirectPlayCommand::AddForwardRequest,
            taMakeReplyAddress(player.tcpPort(), {127, 0, 0, 1}),
            addForwardRequestPayload(systemPlayerId, player.tcpPort(), player.udpPortValue(), config.sessionHostId)));
        runUntil(ioContext, [&] { return player.hasCommand(TaDirectPlayCommand::SuperEnumPlayersReply); });
        player.createPlayer(0x08D90E74, "player");
        runUntil(ioContext, [&] { return joined.size() == 1; });

        stranger.deletePlayer(systemPlayerId);
        runUntil(ioContext, [&] { return host.forgedMessages() == 1; });
        REQUIRE(left.empty());

        player.deletePlayer(systemPlayerId);
        runUntil(ioContext, [&] { return left.size() == 1; });
        REQUIRE(left[0] == 0x08D90E74u);

        // The named player's own delete afterwards finds nobody and is quiet.
        player.deletePlayer(0x08D90E74);
        runFor(ioContext, std::chrono::milliseconds(100));
        REQUIRE(left.size() == 1);

        host.stop();
    }

    TEST_CASE("application data in a joined player's name from another address is not that player's (#716)", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });
        std::vector<std::vector<std::uint8_t>> heard;
        host.onAppData([&](TaHostSession::PeerId, const std::vector<std::uint8_t>& bytes, TaTransport) { heard.push_back(bytes); });

        TestJoiner player(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);
        TestJoiner stranger(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp, "127.0.0.2");

        const std::uint32_t playerId = 0x08D90E74;
        player.createPlayer(playerId, "player");
        runUntil(ioContext, [&] { return joined.size() == 1; });

        // Over UDP and over TCP, stamped with the player's id.
        stranger.sendAppDataUdp(config.hostPlayerId, {0xDE, 0xAD}, playerId);
        stranger.sendAppDataTcp(config.hostPlayerId, {0xBE, 0xEF}, playerId);
        runUntil(ioContext, [&] { return host.forgedMessages() == 2; });
        REQUIRE(heard.empty());

        // The player's own is passed up.
        player.sendAppDataUdp(config.hostPlayerId, {0x01}, playerId);
        runUntil(ioContext, [&] { return heard.size() == 1; });
        REQUIRE(heard[0] == std::vector<std::uint8_t>{0x01});
        REQUIRE(host.forgedMessages() == 2);

        host.stop();
    }

    TEST_CASE("a second machine cannot take a joined player's id or the host's", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });

        TestJoiner player(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);
        TestJoiner stranger(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp, "127.0.0.2");

        const std::uint32_t playerId = 0x08D90E74;
        player.createPlayer(playerId, "player");
        runUntil(ioContext, [&] { return joined.size() == 1; });
        auto before = host.peerAddress(playerId);
        REQUIRE(before);

        stranger.createPlayer(playerId, "impostor");
        stranger.createPlayer(config.hostPlayerId, "host");
        stranger.createPlayer(config.hostSystemPlayerId, "hostsys");
        runUntil(ioContext, [&] { return host.forgedMessages() == 3; });

        REQUIRE(joined.size() == 1);
        REQUIRE(host.load().peers == 1);
        auto after = host.peerAddress(playerId);
        REQUIRE(after);
        REQUIRE(after->tcp == before->tcp);
        REQUIRE(after->udp == before->udp);
        REQUIRE(host.peerName(playerId) == "player");

        // The same machine saying its own id again is only a re-announce.
        // (The wire holds a name in 20 bytes before the addresses begin, so a
        // name here is nine characters at most.)
        player.createPlayer(playerId, "again");
        runUntil(ioContext, [&] { return host.peerName(playerId) == "again"; });
        REQUIRE(host.load().peers == 1);

        host.stop();
    }

    TEST_CASE("a launched game turns a late joiner away with reason 3 and says it is closed (#716)", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });

        TestJoiner player(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);
        TestJoiner late(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp, "127.0.0.2");

        player.createPlayer(0x08D90E74, "player");
        runUntil(ioContext, [&] { return joined.size() == 1; });

        // The launch closes the room and republishes the description.
        host.setAdmissionClosed(true);
        host.sendSessionDescChanged();
        runUntil(ioContext, [&] {
            // The second SESSIONDESCCHANGED the player has been sent is the one
            // after the close.
            std::size_t count = 0;
            for (const auto* message : player.received())
            {
                if (taIsDirectPlayMessage(message->data(), message->size())
                    && taDecodeDirectPlayMessage(message->data(), message->size()).command == TaDirectPlayCommand::SessionDescChanged)
                {
                    ++count;
                }
            }
            return count >= 2;
        });
        const std::vector<std::uint8_t>* newest = nullptr;
        for (const auto* message : player.received())
        {
            if (taIsDirectPlayMessage(message->data(), message->size())
                && taDecodeDirectPlayMessage(message->data(), message->size()).command == TaDirectPlayCommand::SessionDescChanged)
            {
                newest = message;
            }
        }
        REQUIRE(newest != nullptr);
        auto changed = taDecodeSessionDescChanged(taDecodeDirectPlayMessage(newest->data(), newest->size()).payload);
        REQUIRE((changed.session.flags & TaSessionJoinDisabled) != 0);

        late.createPlayer(0x08D90E72, "late");
        runUntil(ioContext, [&] { return !late.rejects().empty(); });
        REQUIRE(late.rejects()[0] == taBuildReject(0x08D90E72, TaRejectClosed));
        REQUIRE(joined.size() == 1);
        REQUIRE(host.load().peers == 1);

        // A player already in is not turned away for saying so again.
        player.createPlayer(0x08D90E74, "player");
        runFor(ioContext, std::chrono::milliseconds(100));
        REQUIRE(player.rejects().empty());
        REQUIRE(host.load().peers == 1);

        // And the room can be opened again.
        host.setAdmissionClosed(false);
        auto joinedBefore = joined.size();
        late.createPlayer(0x08D90E72, "late");
        runUntil(ioContext, [&] { return host.load().peers == 2; });
        REQUIRE(joined.size() == joinedBefore + 1);

        host.stop();
    }

    TEST_CASE("a joined peer that says nothing is rejected with reason 6 and removed (#717)", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        config.silenceTimeout = std::chrono::milliseconds(250);
        config.silenceCheckInterval = std::chrono::milliseconds(20);
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        std::vector<TaHostSession::PeerId> left;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });
        host.onPeerLeft([&](TaHostSession::PeerId id) { left.push_back(id); });
        host.onAppData([](TaHostSession::PeerId, const std::vector<std::uint8_t>&, TaTransport) {});

        TestJoiner joiner(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);
        const std::uint32_t silentId = 0x08D90E74;
        const std::uint32_t chattyId = 0x08D90E72;
        joiner.createPlayer(silentId, "silent");
        joiner.createPlayer(chattyId, "chatty");
        runUntil(ioContext, [&] { return joined.size() == 2; });

        // One keeps talking, the other does not.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        while (left.empty() && std::chrono::steady_clock::now() < deadline)
        {
            joiner.sendAppDataUdp(config.hostPlayerId, {0x06}, chattyId);
            runFor(ioContext, std::chrono::milliseconds(20));
        }

        REQUIRE(left == std::vector<TaHostSession::PeerId>{silentId});
        REQUIRE(host.silentPeersDropped() == 1);
        REQUIRE(host.load().peers == 1);

        // It was told why, and told the others as a leaving player is.
        runUntil(ioContext, [&] { return !joiner.rejects().empty(); });
        REQUIRE(joiner.rejects()[0] == taBuildReject(silentId, TaRejectSilent));
        runUntil(ioContext, [&] { return joiner.hasCommand(TaDirectPlayCommand::DeletePlayer); });

        host.stop();
    }

    TEST_CASE("a game name and a map name that are too long are cut to the 31 the original keeps", "[ta][host]")
    {
        asio::io_context ioContext;
        TaHostConfig config;
        config.ports = TaHostPorts{0, 0, 0};
        config.gameName = "a game name that runs on and on";
        config.mapName = "a map name that is far too long to fit";
        TaHostSession host(ioContext, config);
        host.start();

        std::vector<TaHostSession::PeerId> joined;
        host.onPeerJoined([&](TaHostSession::PeerId id) { joined.push_back(id); });

        TestJoiner joiner(ioContext, host.localPorts().gameTcp, host.localPorts().gameUdp);
        joiner.createPlayer(0x08D90E74, "nine char");
        runUntil(ioContext, [&] { return joined.size() == 1 && joiner.hasCommand(TaDirectPlayCommand::SessionDescChanged); });

        auto* message = joiner.findCommand(TaDirectPlayCommand::SessionDescChanged);
        auto changed = taDecodeSessionDescChanged(taDecodeDirectPlayMessage(message->data(), message->size()).payload);
        REQUIRE(changed.name.size() == TaMaxSessionNameLength);
        REQUIRE(changed.name.substr(0, 16) == "a game name that");

        host.stop();
    }

    TEST_CASE("two sessions do not share a GUID", "[ta][host]")
    {
        TaHostConfig first;
        TaHostConfig second;
        REQUIRE(first.instanceGuid != second.instanceGuid);
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
