#include "TaHostSession.h"
#include <algorithm>
#include <cstring>

namespace rwe
{
    namespace
    {
        constexpr std::size_t MaxBufferedBytes = 1 << 20;
        constexpr std::size_t MaxOutboundQueueBytes = 4 << 20;
        constexpr std::size_t MaxOutboundConnections = 64;

        std::string endpointKey(const asio::ip::tcp::endpoint& endpoint)
        {
            return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
        }

        std::vector<std::uint8_t> ipBytes(const asio::ip::address& address)
        {
            if (address.is_v4())
            {
                auto bytes = address.to_v4().to_bytes();
                return {bytes[0], bytes[1], bytes[2], bytes[3]};
            }
            return {0, 0, 0, 0};
        }

        /**
         * A joiner's game address with the packet's source IP: it chooses its
         * own port, but never a third party's address to send to.
         */
        TaDpAddress addressWithSourceIp(const TaDpAddress& address, const asio::ip::address& sourceIp)
        {
            auto result = address;
            auto ip = ipBytes(sourceIp);
            std::copy(ip.begin(), ip.end(), result.ip.begin());
            return result;
        }

        TaSuperPackedPlayer makeSystemPlayer(std::uint32_t id, std::uint32_t flags = 5)
        {
            TaSuperPackedPlayer player;
            player.flags = flags;
            player.id = id;
            player.infoMask = 0x4;
            player.versionOrSystemPlayerId = TaDirectPlayDialect;
            return player;
        }

        TaSuperPackedPlayer makeNamedPlayer(std::uint32_t id, const std::string& name, std::uint32_t systemId)
        {
            TaSuperPackedPlayer player;
            player.flags = 12;
            player.id = id;
            player.infoMask = 0x17;
            player.versionOrSystemPlayerId = systemId;
            player.shortName = name;
            player.longName = name;
            return player;
        }
    }

    TaHostPorts TaHostPorts::atBase(int base)
    {
        return TaHostPorts{
            static_cast<std::uint16_t>(47624 + base),
            static_cast<std::uint16_t>(2300 + base),
            static_cast<std::uint16_t>(2350 + base)};
    }

    TaHostSession::TaHostSession(asio::io_context& ioContext, TaHostConfig config)
        : ioContext(ioContext),
          config(std::move(config)),
          ports(this->config.ports),
          enumTcpAcceptor(ioContext),
          gameTcpAcceptor(ioContext),
          enumUdpSocket(ioContext),
          gameUdpSocket(ioContext),
          pruneTimer(ioContext),
          nextPlayerId(this->config.firstAssignedPlayerId),
          nextSystemPlayerId(this->config.firstAssignedSystemPlayerId)
    {
    }

    TaHostSession::~TaHostSession()
    {
        stop();
    }

    void TaHostSession::start()
    {
        if (started)
        {
            return;
        }
        started = true;

        auto bindTcp = [](asio::ip::tcp::acceptor& acceptor, std::uint16_t port) {
            acceptor.open(asio::ip::tcp::v4());
            acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port));
            acceptor.listen(asio::socket_base::max_listen_connections);
            return acceptor.local_endpoint().port();
        };

        auto bindUdp = [](asio::ip::udp::socket& socket, std::uint16_t port) {
            socket.open(asio::ip::udp::v4());
            socket.set_option(asio::ip::udp::socket::reuse_address(true));
            socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port));
            return socket.local_endpoint().port();
        };

        ports.enumSessions = bindTcp(enumTcpAcceptor, ports.enumSessions);
        ports.gameTcp = bindTcp(gameTcpAcceptor, ports.gameTcp);
        // The enum UDP socket shares the enum TCP port's number.
        bindUdp(enumUdpSocket, ports.enumSessions);
        ports.gameUdp = bindUdp(gameUdpSocket, ports.gameUdp);

        listenEnumTcp();
        listenGameTcp();
        listenEnumUdp();
        listenGameUdp();
        prunePending();
    }

    void TaHostSession::stop()
    {
        if (!started)
        {
            return;
        }
        started = false;

        asio::error_code ignored;
        enumTcpAcceptor.close(ignored);
        gameTcpAcceptor.close(ignored);
        enumUdpSocket.close(ignored);
        gameUdpSocket.close(ignored);
        pruneTimer.cancel();
        for (auto& [key, link] : outbound)
        {
            (void)key;
            link->socket.close(ignored);
        }
        outbound.clear();
        for (auto& connection : incoming)
        {
            connection->socket.close(ignored);
            connection->idleTimer.cancel();
        }
        incoming.clear();
        pending.clear();
    }

    void TaHostSession::onPeerJoined(PeerJoinedHandler handler)
    {
        peerJoinedHandler = std::move(handler);
    }

    void TaHostSession::onPeerLeft(PeerLeftHandler handler)
    {
        peerLeftHandler = std::move(handler);
    }

    void TaHostSession::onAppData(AppDataHandler handler)
    {
        appDataHandler = std::move(handler);
    }

    void TaHostSession::setOptions(std::uint8_t options)
    {
        config.options = options;
    }

    std::optional<TaPeerAddress> TaHostSession::peerAddress(PeerId peerId) const
    {
        auto it = peers.find(peerId);
        if (it == peers.end())
        {
            return std::nullopt;
        }
        return TaPeerAddress{it->second.playerId, it->second.tcpEndpoint, it->second.udpEndpoint};
    }

    std::string TaHostSession::peerName(PeerId peerId) const
    {
        auto it = peers.find(peerId);
        if (it == peers.end())
        {
            return {};
        }
        return it->second.shortName;
    }

    void TaHostSession::listenEnumTcp()
    {
        auto connection = std::make_shared<IncomingTcp>(ioContext);
        enumTcpAcceptor.async_accept(connection->socket, [this, connection](const asio::error_code& error) {
            if (!error)
            {
                acceptIncoming(connection);
            }
            if (started)
            {
                listenEnumTcp();
            }
        });
    }

    void TaHostSession::listenGameTcp()
    {
        auto connection = std::make_shared<IncomingTcp>(ioContext);
        gameTcpAcceptor.async_accept(connection->socket, [this, connection](const asio::error_code& error) {
            if (!error)
            {
                acceptIncoming(connection);
            }
            if (started)
            {
                listenGameTcp();
            }
        });
    }

    void TaHostSession::acceptIncoming(std::shared_ptr<IncomingTcp> connection)
    {
        // Each accepted socket carries a 64 KB read buffer, so an unbounded
        // accept queue is an unbounded allocation; over the cap the connection
        // is closed unread.
        if (incoming.size() >= MaxIncomingConnections)
        {
            asio::error_code ignored;
            connection->socket.close(ignored);
            return;
        }

        incoming.push_back(connection);
        armIdleTimeout(connection);
        readTcp(connection);
    }

    void TaHostSession::armIdleTimeout(std::shared_ptr<IncomingTcp> connection)
    {
        connection->idleTimer.expires_after(config.incomingIdleTimeout);
        connection->idleTimer.async_wait([this, connection](const asio::error_code& error) {
            if (error)
            {
                return;
            }
            asio::error_code ignored;
            connection->socket.close(ignored);
        });
    }

    void TaHostSession::forgetIncoming(const std::shared_ptr<IncomingTcp>& connection)
    {
        incoming.erase(std::remove(incoming.begin(), incoming.end(), connection), incoming.end());
    }

    TaHostSession::PendingHandshake* TaHostSession::pendingFor(const std::string& key)
    {
        auto it = pending.find(key);
        if (it != pending.end())
        {
            return &it->second;
        }

        // A handshake key is a reply address the sender picks, so a flood of
        // REQUESTPLAYERID from fresh ports would otherwise grow this map for
        // ever; the oldest handshake is evicted to make room.
        if (pending.size() >= MaxPendingHandshakes)
        {
            pending.erase(pending.begin());
        }

        auto [inserted, ok] = pending.emplace(key, PendingHandshake{});
        if (!ok)
        {
            return nullptr;
        }
        inserted->second.started = std::chrono::steady_clock::now();
        return &inserted->second;
    }

    void TaHostSession::prunePending()
    {
        auto cutoff = std::chrono::steady_clock::now() - config.pendingHandshakeTimeout;
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->second.started < cutoff)
            {
                it = pending.erase(it);
            }
            else
            {
                ++it;
            }
        }

        pruneTimer.expires_after(config.pendingHandshakeTimeout);
        pruneTimer.async_wait([this](const asio::error_code& error) {
            if (error || !started)
            {
                return;
            }
            prunePending();
        });
    }

    TaHostSession::Load TaHostSession::load() const
    {
        return Load{peers.size(), pending.size(), incoming.size()};
    }

    void TaHostSession::listenEnumUdp()
    {
        auto sender = std::make_shared<asio::ip::udp::endpoint>();
        enumUdpSocket.async_receive_from(
            asio::buffer(udpBuffer.data(), udpBuffer.size()),
            *sender,
            [this, sender](const asio::error_code& error, std::size_t bytes) {
                if (!error)
                {
                    handleUdp(udpBuffer.data(), bytes, *sender);
                }
                if (started)
                {
                    listenEnumUdp();
                }
            });
    }

    void TaHostSession::listenGameUdp()
    {
        auto sender = std::make_shared<asio::ip::udp::endpoint>();
        gameUdpSocket.async_receive_from(
            asio::buffer(udpBuffer.data(), udpBuffer.size()),
            *sender,
            [this, sender](const asio::error_code& error, std::size_t bytes) {
                if (!error)
                {
                    handleUdp(udpBuffer.data(), bytes, *sender);
                }
                if (started)
                {
                    listenGameUdp();
                }
            });
    }

    void TaHostSession::readTcp(std::shared_ptr<IncomingTcp> connection)
    {
        connection->socket.async_read_some(
            asio::buffer(connection->scratch.data(), connection->scratch.size()),
            [this, connection](const asio::error_code& error, std::size_t bytes) {
                if (error)
                {
                    forgetIncoming(connection);
                    return;
                }
                handleTcpBytes(connection, connection->scratch.data(), bytes);
                if (connection->socket.is_open())
                {
                    armIdleTimeout(connection);
                    readTcp(connection);
                }
                else
                {
                    forgetIncoming(connection);
                }
            });
    }

    void TaHostSession::handleTcpBytes(
        std::shared_ptr<IncomingTcp> connection,
        const std::uint8_t* data,
        std::size_t len)
    {
        if (connection->pending.size() + len > MaxBufferedBytes)
        {
            asio::error_code ignored;
            connection->socket.close(ignored);
            return;
        }
        connection->pending.insert(connection->pending.end(), data, data + len);

        asio::error_code error;
        auto sourceIp = connection->socket.remote_endpoint(error).address();

        while (connection->pending.size() >= 4)
        {
            auto size = taPeekDirectPlayMessageSize(connection->pending.data(), connection->pending.size());
            if (size < 4)
            {
                // Not a DirectPlay stream at all; there is no way to resync.
                asio::error_code ignored;
                connection->socket.close(ignored);
                return;
            }
            if (size > connection->pending.size())
            {
                return;
            }

            std::vector<std::uint8_t> message(
                connection->pending.begin(),
                connection->pending.begin() + static_cast<std::ptrdiff_t>(size));
            connection->pending.erase(
                connection->pending.begin(),
                connection->pending.begin() + static_cast<std::ptrdiff_t>(size));

            try
            {
                if (taIsDirectPlayMessage(message.data(), message.size()))
                {
                    handleSystemMessage(message, sourceIp);
                }
                else
                {
                    handleAppData(message.data(), message.size(), true);
                }
            }
            catch (const std::exception&)
            {
                // A malformed message costs that message only.
            }
        }
    }

    void TaHostSession::handleUdp(
        const std::uint8_t* data,
        std::size_t len,
        const asio::ip::udp::endpoint& source)
    {
        try
        {
            if (taIsDirectPlayMessage(data, len))
            {
                handleSystemMessage(std::vector<std::uint8_t>(data, data + len), source.address());
            }
            else
            {
                handleAppData(data, len, false);
            }
        }
        catch (const std::exception&)
        {
            // A malformed datagram costs that datagram only.
        }
    }

    void TaHostSession::handleSystemMessage(
        const std::vector<std::uint8_t>& bytes,
        const asio::ip::address& sourceIp)
    {
        auto message = taDecodeDirectPlayMessage(bytes.data(), bytes.size());
        auto reply = resolveReplyEndpoint(message.header.reply, sourceIp, ports.gameTcp);

        switch (message.command)
        {
            case TaDirectPlayCommand::EnumSessions:
                handleEnumSessions(reply);
                break;
            case TaDirectPlayCommand::RequestPlayerId:
                handleRequestPlayerId(message.payload, reply);
                break;
            case TaDirectPlayCommand::AddForwardRequest:
                handleAddForwardRequest(message.payload, reply);
                break;
            case TaDirectPlayCommand::CreatePlayer:
                handleCreatePlayer(message.payload, reply);
                break;
            case TaDirectPlayCommand::DeletePlayer:
                handleDeletePlayer(message.payload);
                break;
            case TaDirectPlayCommand::Ping:
                // Pings are issue #423's out-of-scope list.
                break;
            default:
                break;
        }
    }

    void TaHostSession::handleAppData(const std::uint8_t* data, std::size_t len, bool tcp)
    {
        TaAppData app = tcp ? taDecodeAppDataTcp(data, len) : taDecodeAppDataUdp(data, len);

        auto it = peers.find(app.from);
        if (it == peers.end())
        {
            return;
        }

        if (appDataHandler)
        {
            appDataHandler(app.from, app.payload, tcp ? TaTransport::Tcp : TaTransport::Udp);
        }
    }

    void TaHostSession::handleEnumSessions(const asio::ip::tcp::endpoint& reply)
    {
        auto payload = taEncodeEnumSessionsReply(
            makeSessionDescription(),
            taSessionName(config.gameName, config.mapName));
        sendSystemTo(reply, TaDirectPlayCommand::EnumSessionsReply, payload);
    }

    void TaHostSession::handleRequestPlayerId(
        std::span<const std::uint8_t> payload,
        const asio::ip::tcp::endpoint& reply)
    {
        auto flags = taDecodeRequestPlayerId(payload);
        auto isSystemPlayer = (flags & 0x1) != 0;
        // Down in pairs, as a real host hands them out: counting up would give
        // a second joiner the host's own ids, which sit just above the first.
        auto id = isSystemPlayer ? nextSystemPlayerId : nextPlayerId;
        (isSystemPlayer ? nextSystemPlayerId : nextPlayerId) -= 2;

        if (isSystemPlayer)
        {
            if (auto* handshake = pendingFor(endpointKey(reply)))
            {
                handshake->systemPlayerId = id;
            }
        }

        auto body = taEncodeRequestPlayerReply(id);
        sendSystemTo(reply, TaDirectPlayCommand::RequestPlayerReply, body);
    }

    void TaHostSession::handleAddForwardRequest(
        std::span<const std::uint8_t> payload,
        const asio::ip::tcp::endpoint& reply)
    {
        auto request = taDecodeAddForwardRequest(payload);
        auto* handshake = pendingFor(endpointKey(reply));
        if (handshake == nullptr)
        {
            return;
        }
        handshake->systemPlayerId = request.systemPlayerId;

        auto tcpAddress = addressWithSourceIp(request.tcpAddress, reply.address());
        auto udpAddress = addressWithSourceIp(request.udpAddress, reply.address());
        handshake->tcpEndpoint = asio::ip::tcp::endpoint(
            asio::ip::address_v4(tcpAddress.ip),
            tcpAddress.port != 0 ? tcpAddress.port : ports.gameTcp);
        handshake->udpEndpoint = asio::ip::udp::endpoint(
            asio::ip::address_v4(udpAddress.ip),
            udpAddress.port != 0 ? udpAddress.port : ports.gameUdp);

        auto joiner = makeSystemPlayer(request.systemPlayerId);
        joiner.tcpAddress = request.tcpAddress;
        joiner.udpAddress = request.udpAddress;
        joiner.tcpAddress.port = handshake->tcpEndpoint.port();
        joiner.udpAddress.port = handshake->udpEndpoint.port();
        auto ip = ipBytes(handshake->tcpEndpoint.address());
        std::copy(ip.begin(), ip.end(), joiner.tcpAddress.ip.begin());
        std::copy(ip.begin(), ip.end(), joiner.udpAddress.ip.begin());

        auto body = taEncodeSuperEnumPlayersReply(makeSuperEnum(&joiner));
        sendSystemTo(reply, TaDirectPlayCommand::SuperEnumPlayersReply, body);
    }

    void TaHostSession::handleCreatePlayer(
        std::span<const std::uint8_t> payload,
        const asio::ip::tcp::endpoint& reply)
    {
        auto created = taDecodeCreatePlayer(payload);

        auto key = endpointKey(reply);
        auto handshakeIt = pending.find(key);

        // The session advertises maxPlayers, the host counts as one, and the
        // player is either new or is replacing itself; a full session refuses
        // the joiner rather than growing past what it advertised.
        if (peers.find(created.playerId) == peers.end() && peers.size() + 1 >= MaxPlayers)
        {
            pending.erase(key);
            return;
        }

        Peer peer;
        peer.playerId = created.playerId;
        peer.shortName = created.name;
        peer.longName = created.name;

        if (handshakeIt != pending.end())
        {
            peer.systemPlayerId = handshakeIt->second.systemPlayerId;
            peer.tcpEndpoint = handshakeIt->second.tcpEndpoint;
            peer.udpEndpoint = handshakeIt->second.udpEndpoint;
        }
        else
        {
            auto tcpAddress = addressWithSourceIp(created.tcpAddress, reply.address());
            auto udpAddress = addressWithSourceIp(created.udpAddress, reply.address());
            peer.tcpEndpoint = asio::ip::tcp::endpoint(asio::ip::address_v4(tcpAddress.ip), tcpAddress.port);
            peer.udpEndpoint = asio::ip::udp::endpoint(asio::ip::address_v4(udpAddress.ip), udpAddress.port);
        }

        peers[peer.playerId] = peer;
        pending.erase(key);

        sendSessionDescChanged();

        if (peerJoinedHandler)
        {
            peerJoinedHandler(peer.playerId);
        }
    }

    void TaHostSession::handleDeletePlayer(std::span<const std::uint8_t> payload)
    {
        auto playerId = taDecodeDeletePlayer(payload);
        auto erased = peers.erase(playerId);
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->second.systemPlayerId == playerId)
            {
                it = pending.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (erased != 0 && peerLeftHandler)
        {
            peerLeftHandler(playerId);
        }
    }

    std::shared_ptr<TaHostSession::TcpLink> TaHostSession::linkFor(const asio::ip::tcp::endpoint& endpoint)
    {
        auto key = endpointKey(endpoint);
        auto it = outbound.find(key);
        if (it != outbound.end())
        {
            return it->second;
        }

        if (outbound.size() >= MaxOutboundConnections)
        {
            return nullptr;
        }

        auto link = std::make_shared<TcpLink>(ioContext);
        outbound.emplace(key, link);
        link->socket.async_connect(endpoint, [this, link, key](const asio::error_code& error) {
            if (error)
            {
                auto found = outbound.find(key);
                if (found != outbound.end() && found->second == link)
                {
                    outbound.erase(found);
                }
                return;
            }
            link->connected = true;
            writeNext(link, key);
        });
        return link;
    }

    void TaHostSession::enqueue(
        std::shared_ptr<TcpLink> link,
        std::vector<std::uint8_t> bytes,
        const std::string& key)
    {
        if (!link)
        {
            return;
        }

        std::size_t queued = 0;
        for (const auto& message : link->queue)
        {
            queued += message->size();
        }
        if (queued + bytes.size() > MaxOutboundQueueBytes)
        {
            asio::error_code ignored;
            link->socket.close(ignored);
            outbound.erase(key);
            return;
        }

        link->queue.push_back(std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes)));
        if (link->connected)
        {
            writeNext(link, key);
        }
    }

    void TaHostSession::writeNext(const std::shared_ptr<TcpLink>& link, const std::string& key)
    {
        if (!link || link->writing || link->queue.empty() || !link->connected)
        {
            return;
        }

        link->writing = true;
        auto message = link->queue.front();
        asio::async_write(link->socket, asio::buffer(*message), [this, link, key](const asio::error_code& error, std::size_t) {
            link->writing = false;
            if (error)
            {
                outbound.erase(key);
                asio::error_code ignored;
                link->socket.close(ignored);
                return;
            }
            link->queue.pop_front();
            writeNext(link, key);
        });
    }

    void TaHostSession::sendSystemTo(
        const asio::ip::tcp::endpoint& endpoint,
        TaDirectPlayCommand command,
        std::span<const std::uint8_t> payload)
    {
        auto link = linkFor(endpoint);
        if (!link)
        {
            return;
        }
        enqueue(link, taEncodeSystemMessage(command, taMakeReplyAddress(ports.gameTcp), payload), endpointKey(endpoint));
    }

    bool TaHostSession::send(PeerId to, std::span<const std::uint8_t> bytes, TaTransport transport)
    {
        auto it = peers.find(to);
        if (it == peers.end())
        {
            return false;
        }

        if (transport == TaTransport::Tcp)
        {
            auto link = linkFor(it->second.tcpEndpoint);
            if (!link)
            {
                return false;
            }
            enqueue(
                link,
                taEncodeAppDataTcp(taMakeReplyAddress(ports.gameTcp), config.hostPlayerId, to, bytes),
                endpointKey(it->second.tcpEndpoint));
        }
        else
        {
            auto message = taEncodeAppDataUdp(config.hostPlayerId, to, bytes);
            asio::error_code error;
            gameUdpSocket.send_to(asio::buffer(message), it->second.udpEndpoint, 0, error);
            if (error)
            {
                return false;
            }
        }

        return true;
    }

    void TaHostSession::sendToAll(std::span<const std::uint8_t> bytes, TaTransport transport)
    {
        for (const auto& [id, peer] : peers)
        {
            (void)peer;
            send(id, bytes, transport);
        }
    }

    void TaHostSession::sendSessionDescChanged()
    {
        TaSessionDescChanged message;
        message.session = makeSessionDescription();
        message.name = taSessionName(config.gameName, config.mapName);
        auto body = taEncodeSessionDescChanged(message);

        for (const auto& [id, peer] : peers)
        {
            (void)id;
            sendSystemTo(peer.tcpEndpoint, TaDirectPlayCommand::SessionDescChanged, body);
        }
    }

    void TaHostSession::deletePeer(PeerId peerId)
    {
        auto it = peers.find(peerId);
        if (it == peers.end())
        {
            return;
        }

        auto playerBody = taEncodeDeletePlayer(it->second.playerId);
        auto systemBody = taEncodeDeletePlayer(it->second.systemPlayerId);
        for (const auto& [id, peer] : peers)
        {
            (void)id;
            sendSystemTo(peer.tcpEndpoint, TaDirectPlayCommand::DeletePlayer, playerBody);
            sendSystemTo(peer.tcpEndpoint, TaDirectPlayCommand::DeletePlayer, systemBody);
        }
        sendSystemTo(it->second.tcpEndpoint, TaDirectPlayCommand::DeletePlayer, playerBody);
        sendSystemTo(it->second.tcpEndpoint, TaDirectPlayCommand::DeletePlayer, systemBody);

        peers.erase(it);
        if (peerLeftHandler)
        {
            peerLeftHandler(peerId);
        }
    }

    void TaHostSession::sendHostDeleted(std::uint32_t playerId, std::uint32_t systemPlayerId)
    {
        auto playerBody = taEncodeDeletePlayer(playerId);
        auto systemBody = taEncodeDeletePlayer(systemPlayerId);
        for (const auto& [id, peer] : peers)
        {
            (void)id;
            sendSystemTo(peer.tcpEndpoint, TaDirectPlayCommand::DeletePlayer, playerBody);
            sendSystemTo(peer.tcpEndpoint, TaDirectPlayCommand::DeletePlayer, systemBody);
        }
    }

    TaSessionDescription TaHostSession::makeSessionDescription() const
    {
        TaSessionDescription session;
        session.instanceGuid = config.instanceGuid;
        session.applicationGuid = TaApplicationGuid;
        session.maxPlayers = static_cast<std::uint32_t>(MaxPlayers);
        session.currentPlayers = static_cast<std::uint32_t>(1 + peers.size());
        session.hostPlayerId = config.sessionHostId;
        // A real host's reads 0x4f010001: the options byte on top, a constant
        // 1 below it, and the player count at the bottom.
        session.user3 = 0x00010000u | (session.currentPlayers & 0xFFu);
        session.setOptions(config.options);
        return session;
    }

    TaSuperEnumPlayersReply TaHostSession::makeSuperEnum(const TaSuperPackedPlayer* extraSystemPlayer) const
    {
        TaSuperEnumPlayersReply reply;
        reply.session = makeSessionDescription();
        reply.name = taSessionName(config.gameName, config.mapName);

        if (extraSystemPlayer != nullptr)
        {
            reply.players.push_back(*extraSystemPlayer);
        }

        // 0x0f, not a joiner's 0x05: the name-server bit is how a joiner finds
        // the host to send its CREATEPLAYER to, and a real host sets it.
        auto hostSystem = makeSystemPlayer(config.hostSystemPlayerId, 0x0F);
        hostSystem.tcpAddress.port = ports.gameTcp;
        hostSystem.udpAddress.port = ports.gameUdp;
        reply.players.push_back(hostSystem);

        auto hostPlayer = makeNamedPlayer(config.hostPlayerId, config.gameName, config.hostSystemPlayerId);
        // A real host's player carries 21 bytes of TA's own player data; the
        // captures only show this value, so it is sent as they show it.
        hostPlayer.playerData.assign(21, 0);
        hostPlayer.playerData[19] = 0x50;
        hostPlayer.tcpAddress.port = ports.gameTcp;
        hostPlayer.udpAddress.port = ports.gameUdp;
        reply.players.push_back(hostPlayer);

        for (const auto& [id, peer] : peers)
        {
            (void)id;
            auto player = makeNamedPlayer(peer.playerId, peer.shortName, peer.systemPlayerId);
            player.tcpAddress.port = peer.tcpEndpoint.port();
            player.udpAddress.port = peer.udpEndpoint.port();
            auto tcpBytes = peer.tcpEndpoint.address().to_v4().to_bytes();
            auto udpBytes = peer.udpEndpoint.address().to_v4().to_bytes();
            std::copy(tcpBytes.begin(), tcpBytes.end(), player.tcpAddress.ip.begin());
            std::copy(udpBytes.begin(), udpBytes.end(), player.udpAddress.ip.begin());
            reply.players.push_back(player);
        }

        return reply;
    }

    asio::ip::tcp::endpoint TaHostSession::resolveReplyEndpoint(
        const TaDpAddress& address,
        const asio::ip::address& sourceIp,
        std::uint16_t fallbackPort) const
    {
        // Always the packet's source IP, never the one in the header: a header
        // IP that differs from the source would otherwise turn the host into a
        // reflector sending to a third party. Only the port is taken from it.
        auto port = address.port != 0 ? address.port : fallbackPort;
        return asio::ip::tcp::endpoint(sourceIp, port);
    }

    std::vector<std::uint8_t> TaHostSession::addressIp(const asio::ip::address& address)
    {
        return ipBytes(address);
    }
}
