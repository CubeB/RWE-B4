#include "TaHostGame.h"

#include <asio.hpp>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/net/ta/TaDirectPlay.h>
#include <rwe/net/ta/TaOutboundBatcher.h>
#include <rwe/net/ta/TaPinger.h>
#include <rwe/util/SimpleLogger.h>
#include <algorithm>
#include <future>
#include <mutex>
#include <thread>
#include <utility>

namespace rwe
{
    namespace
    {
        /**
         * The clock a 0x02 carries: milliseconds, near GetTickCount. A wall
         * clock and not the game tick -- the two peers in a capture agree to
         * the millisecond, and the gap between the two ticks in a reply is the
         * round trip.
         */
        std::uint32_t nowMs()
        {
            return static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }

        /**
         * The cause a quitting player's commander dies with, and the corpse it
         * leaves: level 0, so nothing. The recorded host's own quit
         * (ta-baseline.pcap at 92.91) reads as unit 251, the host's own player
         * id as the killer, the unit itself, severity 100 and 0x80 -- cause 8
         * and no corpse.
         */
        constexpr std::uint8_t TaQuitCauseAndLevel = 0x80;
        constexpr std::uint8_t TaQuitSeverity = 100;

        /**
         * The joiner is the session's second player, and a demo's `sender` is
         * the player whose block the ids are in, so the second player is
         * sender 1 whichever block its ids turn out to be in.
         */
        constexpr std::uint8_t JoinerSender = 1;
    }

    struct TaHostGame::Impl
    {
        struct Outbound
        {
            std::uint32_t peer{0};
            std::vector<std::uint8_t> bytes;
            TaTransport transport{TaTransport::Udp};
        };

        explicit Impl(TaHostGameConfig config) : config(std::move(config)) {}

        ~Impl()
        {
            stop();
        }

        /**
         * Messages the game thread has not had sent. Five a second is thirty
         * ticks of a game; thousands of them is a game thread that has stopped
         * for a minute, and dropping the oldest is what a peer would see as
         * loss in any case.
         */
        static constexpr std::size_t MaxOutboundQueued = 8192;

        /** Packets the game thread has not taken; the receiver's own bound is well below this. */
        static constexpr std::size_t MaxInboundQueued = 4096;

        TaHostGameConfig config;

        /** Guards lobby, the two queues and the stats. */
        mutable std::mutex mutex;
        TaHostLobbyState lobby;
        std::deque<TaHostInbound> inbound;
        std::deque<Outbound> outbound;
        TaHostGameStats stats;
        bool leaving{false};

        // Below here belongs to the network thread alone.
        asio::io_context io;
        std::unique_ptr<TaHostSession> session;
        std::unique_ptr<TaBattleroom> room;
        std::unique_ptr<TaOutboundBatcher> traffic;
        std::unique_ptr<TaOutboundBatcher> pingTraffic;
        std::unique_ptr<TaPinger> pings;
        asio::steady_timer gamePingTimer{io};
        std::thread thread;

        std::uint32_t hostPlayerId{0};
        std::uint32_t hostSystemPlayerId{0};

        void start();
        void stop();
        void run();
        void drainOutbound();
        void armGamePings();
        void onGamePing();

        /** A message the game thread queued, for the network thread to send. */
        void enqueue(std::uint32_t peer, std::span<const std::uint8_t> bytes, TaTransport transport);
        void pushInbound(TaHostInbound&& item);
        void noteRefusal(std::string reason);
        void refreshLobby();
    };

    void TaHostGame::Impl::enqueue(std::uint32_t peer, std::span<const std::uint8_t> bytes, TaTransport transport)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (outbound.size() >= MaxOutboundQueued)
            {
                outbound.pop_front();
                ++stats.outboundDropped;
            }
            outbound.push_back(Outbound{peer, std::vector<std::uint8_t>(bytes.begin(), bytes.end()), transport});
        }
        asio::post(io, [this]() { drainOutbound(); });
    }

    void TaHostGame::Impl::pushInbound(TaHostInbound&& item)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (inbound.size() >= MaxInboundQueued)
        {
            inbound.pop_front();
            ++stats.inboundDropped;
        }
        inbound.push_back(std::move(item));
    }

    void TaHostGame::Impl::noteRefusal(std::string reason)
    {
        std::lock_guard<std::mutex> lock(mutex);
        lobby.refusal = std::move(reason);
    }

    void TaHostGame::Impl::drainOutbound()
    {
        std::deque<Outbound> batch;
        {
            std::lock_guard<std::mutex> lock(mutex);
            batch.swap(outbound);
        }

        for (const auto& message : batch)
        {
            session->send(message.peer, message.bytes, message.transport);
        }
    }

    void TaHostGame::Impl::armGamePings()
    {
        gamePingTimer.expires_after(std::chrono::seconds(2));
        gamePingTimer.async_wait([this](const asio::error_code& error) {
            if (error)
            {
                return;
            }
            onGamePing();
            armGamePings();
        });
    }

    void TaHostGame::Impl::onGamePing()
    {
        // A peer whose pings go unanswered is offered for rejection, and a
        // reply has to be built now: a replayed one carries another session's
        // clock and reads as an absurd latency.
        if (!room)
        {
            return;
        }
        auto ids = room->peerIds();
        if (ids.empty())
        {
            return;
        }
        pings->sendRequests(ids);
        pingTraffic->flush();
    }

    void TaHostGame::Impl::refreshLobby()
    {
        std::vector<TaHostLobbyPeer> peers;
        std::optional<TaBattleroom::JoinerInfo> launched;
        if (room)
        {
            auto ids = room->peerIds();
            for (auto id : ids)
            {
                if (auto peer = room->peer(id))
                {
                    peers.push_back(TaHostLobbyPeer{peer->name, peer->side, peer->colour, peer->team, peer->ready});
                }
            }

            if (room->state() == TaBattleroomState::Launched && !ids.empty() && session)
            {
                if (auto address = session->peerAddress(ids.front()))
                {
                    TaBattleroom::JoinerInfo info;
                    info.address = *address;
                    if (auto peer = room->peer(ids.front()))
                    {
                        info.team = peer->team;
                        info.name = peer->name;
                        info.side = peer->side;
                        info.colour = peer->colour;
                    }
                    launched = info;
                }
            }
        }

        std::lock_guard<std::mutex> lock(mutex);
        lobby.peers = std::move(peers);
        if (room)
        {
            lobby.phase = room->state();
        }
        if (launched)
        {
            lobby.launched = *launched;
        }
    }

    void TaHostGame::Impl::start()
    {
        TaHostConfig hostConfig;
        hostConfig.gameName = config.gameName;
        hostConfig.mapName = config.mapName;
        hostConfig.options = config.options;
        hostConfig.ports = TaHostPorts::atBase(config.portBase);
        hostPlayerId = hostConfig.hostPlayerId;
        hostSystemPlayerId = hostConfig.hostSystemPlayerId;

        session = std::make_unique<TaHostSession>(io, std::move(hostConfig));
        session->start();

        {
            std::lock_guard<std::mutex> lock(mutex);
            lobby.enumPort = session->localPorts().enumSessions;
            lobby.gameTcpPort = session->localPorts().gameTcp;
            lobby.gameUdpPort = session->localPorts().gameUdp;
        }

        traffic = std::make_unique<TaOutboundBatcher>(
            [this](TaHostSession::PeerId id, std::span<const std::uint8_t> bytes, TaTransport transport) {
                enqueue(id, bytes, transport);
            },
            TadPacketCompressed);

        // A 0x02 is 20 bytes on the wire and does not compress, which is how the
        // captures carry it, so the pinger frames its own.
        pingTraffic = std::make_unique<TaOutboundBatcher>(
            [this](TaHostSession::PeerId id, std::span<const std::uint8_t> bytes, TaTransport transport) {
                enqueue(id, bytes, transport);
            },
            TadPacketUncompressed);
        pings = std::make_unique<TaPinger>(*pingTraffic, hostPlayerId, nowMs);

        TaBattleroomConfig roomConfig;
        roomConfig.mapName = config.mapName;
        roomConfig.options = config.options;
        roomConfig.hostTeam = config.team;
        roomConfig.side = config.side;
        roomConfig.colour = config.colour;
        room = std::make_unique<TaBattleroom>(io, *session, *traffic, nowMs, std::move(roomConfig));

        session->onPeerJoined([this](TaHostSession::PeerId id) {
            room->peerJoined(id);
            refreshLobby();
            LOG_INFO << "TA host: peer 0x" << std::hex << id << std::dec << " joined";
        });

        session->onPeerLeft([this](TaHostSession::PeerId id) {
            bool inGame = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                inGame = lobby.phase == TaBattleroomState::Launched;
            }

            if (inGame)
            {
                TaHostInbound item;
                item.kind = TaHostInbound::Kind::PeerLeft;
                item.peer = id;
                item.sender = JoinerSender;
                pushInbound(std::move(item));
            }
            else
            {
                room->peerLeft(id);
            }
            refreshLobby();
            LOG_INFO << "TA host: peer 0x" << std::hex << id << std::dec << " left";
        });

        session->onAppData([this](TaHostSession::PeerId id, const std::vector<std::uint8_t>& bytes, TaTransport transport) {
            bool inGame = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                inGame = lobby.phase == TaBattleroomState::Launched;
            }

            if (!inGame)
            {
                // The battleroom reads and answers the subpackets itself, pings
                // included: it holds its own pinger for the lobby.
                room->handleAppData(id, bytes, transport);
                return;
            }

            auto parsed = taParsePacket(bytes);
            if (!parsed)
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++stats.packetsNotTa;
                return;
            }

            // Answered here and now, on the thread that has the sockets, and
            // before the packet is queued: a ping the game thread is late for
            // is a ping the peer reads as a frozen host.
            for (const auto& subPacket : parsed->packet.subpackets)
            {
                if (!subPacket.empty() && subPacket[0] == static_cast<std::uint8_t>(TadSubPacketCode::Ping))
                {
                    pings->handle(id, subPacket, transport);
                }
            }
            pingTraffic->flush();

            TaHostInbound item;
            item.kind = TaHostInbound::Kind::Packet;
            item.peer = id;
            item.sender = JoinerSender;
            item.subPackets = std::move(parsed->packet.subpackets);
            item.packet = std::move(parsed->packet);
            // A packet wearing the reply marker is a reply, which counts for
            // nothing in its sender's own order.
            item.sequence = item.packet.marker == TaReplyMarker
                ? std::nullopt
                : std::optional<std::uint32_t>(item.packet.marker);
            pushInbound(std::move(item));
        });

        room->onReady([this](const TaBattleroomPeer& peer) {
            refreshLobby();
            LOG_INFO << "TA host: peer 0x" << std::hex << peer.playerId << std::dec << " is ready ("
                     << (peer.side == TadSide::Core ? "CORE" : "ARM") << " colour " << static_cast<int>(peer.colour)
                     << ", team " << static_cast<int>(peer.team) << ")";
        });

        room->onRefused([this](const std::string& reason) {
            noteRefusal(reason);
            LOG_WARN << "TA host: launch refused: " << reason;
        });

        room->onLaunched([this](const TaBattleroom::JoinerInfo&) {
            refreshLobby();
            armGamePings();
        });

        thread = std::thread([this]() { run(); });

        LOG_INFO << "TA host: hosting '" << taSessionName(config.gameName, config.mapName) << "' on enum "
                 << session->localPorts().enumSessions << ", tcp " << session->localPorts().gameTcp << ", udp "
                 << session->localPorts().gameUdp;
    }

    void TaHostGame::Impl::run()
    {
        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([this](const asio::error_code&, int) {
            session->stop();
            io.stop();
        });
        io.run();
    }

    void TaHostGame::Impl::stop()
    {
        if (!thread.joinable())
        {
            return;
        }
        session->stop();
        io.stop();
        thread.join();
    }

    TaHostGame::TaHostGame(TaHostGameConfig config)
        : impl(std::make_unique<Impl>(std::move(config)))
    {
        impl->start();
    }

    TaHostGame::~TaHostGame()
    {
        impl->stop();
    }

    const TaHostGameConfig& TaHostGame::config() const
    {
        return impl->config;
    }

    std::uint16_t TaHostGame::commanderUnitId() const
    {
        return tadUnitIdOfIndex(hostUnitBlock, 0, config().maxUnits);
    }

    TaHostLobbyState TaHostGame::lobbyState() const
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        return impl->lobby;
    }

    void TaHostGame::launch(TaBattleroom::LaunchParams params)
    {
        asio::post(impl->io, [this, params = std::move(params)]() {
            if (!impl->room)
            {
                return;
            }
            bool started = impl->room->launch(params);
            impl->noteRefusal(started ? std::string() : std::string("a host rule refused the launch"));
            impl->refreshLobby();
        });
    }

    void TaHostGame::autoLaunchIfReady(TaBattleroom::LaunchParams params)
    {
        if (!config().autoLaunch)
        {
            return;
        }

        auto lobby = lobbyState();
        if (lobby.launched || lobby.phase != TaBattleroomState::Waiting)
        {
            return;
        }
        if (std::none_of(lobby.peers.begin(), lobby.peers.end(), [](const auto& peer) { return peer.ready; }))
        {
            return;
        }
        launch(std::move(params));
    }

    std::vector<TaHostInbound> TaHostGame::takeInbound()
    {
        std::vector<TaHostInbound> taken;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            taken.assign(std::make_move_iterator(impl->inbound.begin()), std::make_move_iterator(impl->inbound.end()));
            impl->inbound.clear();
        }
        return taken;
    }

    void TaHostGame::queueOutbound(std::uint32_t peer, std::span<const std::uint8_t> bytes, TaTransport transport)
    {
        impl->enqueue(peer, bytes, transport);
    }

    void TaHostGame::leave()
    {
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (impl->leaving)
            {
                return;
            }
            impl->leaving = true;
        }

        auto params = impl->config;
        std::promise<void> sent;
        auto done = sent.get_future();
        asio::post(impl->io, [this, params, &sent]() {
            auto& impl = *this->impl;
            if (impl.room && impl.room->state() == TaBattleroomState::Launched)
            {
                auto commanderId = tadUnitIdOfIndex(hostUnitBlock, 0, params.maxUnits);
                impl.traffic->queueForAll(
                    impl.room->peerIds(),
                    tadEncodeDeath(TadDeath{
                        commanderId,
                        impl.hostPlayerId,
                        commanderId,
                        TaQuitSeverity,
                        TaQuitCauseAndLevel}),
                    TaTransport::Udp);
                impl.traffic->flush();
            }
            impl.session->sendHostDeleted(impl.hostPlayerId, impl.hostSystemPlayerId);
            impl.drainOutbound();
            sent.set_value();
        });

        // The quit sequence has to be on the wire before the sockets close, so
        // this waits for the network thread to have sent it rather than
        // stopping the context out from under the work.
        if (done.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        {
            LOG_WARN << "TA host: the quit sequence did not get out in time";
        }
        impl->stop();
    }

    TaHostGameStats TaHostGame::stats() const
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        return impl->stats;
    }
}
