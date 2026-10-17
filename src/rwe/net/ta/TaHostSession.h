#pragma once

// The hosting side of DirectPlay 4's session layer: the sockets a real
// Total Annihilation joins, and the handshake it goes through to get in.
//
// It is a transport and nothing more. What the bytes it carries mean -- the
// battleroom, unit sync, launch -- is built on top of the three things it
// reports: a peer joined, application data arrived from a peer, a peer left.
// The application data is opaque, so those layers never need this header.
//
// Every socket receives only (docs/TA-NETWORK.md, "DirectPlay"): replies go to
// the address in the message's own header, opened as a fresh outbound TCP
// connection, never back down the connection a request arrived on.

#include <asio.hpp>
#include <rwe/net/ta/TaDirectPlay.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rwe
{
    /** Which of the two sockets carried a piece of application data. */
    enum class TaTransport
    {
        Tcp,
        Udp,
    };

    /** The three listening ports, all overridable so a test can use ephemeral ones. */
    struct TaHostPorts
    {
        std::uint16_t enumSessions{47624};
        std::uint16_t gameTcp{2300};
        std::uint16_t gameUdp{2350};

        /**
         * Every port shifted by `base`: the shape `ta_host_probe --port-base`
         * offers so a probe can host beside whatever already holds the real ones.
         */
        static TaHostPorts atBase(int base);
    };

    /**
     * Where one peer's game traffic goes, which a game needs before it can send
     * anything: the sockets it told us it listens on during the handshake.
     */
    struct TaPeerAddress
    {
        /** TaHostSession::PeerId, which is a uint32. */
        std::uint32_t playerId{0};
        asio::ip::tcp::endpoint tcp;
        asio::ip::udp::endpoint udp;
    };

    /**
     * The five things a layer above the session needs from it. TaHostSession is
     * the only implementation in the engine; TaBattleroom takes this rather than
     * the session so its own tests need no sockets.
     */
    struct TaBattleroomHost
    {
        virtual ~TaBattleroomHost() = default;

        /** The id every message the host sends is stamped with. */
        virtual std::uint32_t hostPlayerId() const = 0;

        /** Sends application data to one peer; false for a peer it has lost. */
        virtual bool send(std::uint32_t peer, std::span<const std::uint8_t> bytes, TaTransport transport) = 0;

        /** Tells every peer the session changed, which the host does at launch. */
        virtual void sendSessionDescChanged() = 0;

        /** Where a peer's game traffic goes, or nothing for a peer it has lost. */
        virtual std::optional<TaPeerAddress> peerAddress(std::uint32_t peer) const = 0;

        /** The name CREATEPLAYER carried, or empty for a peer it has lost. */
        virtual std::string peerName(std::uint32_t peer) const = 0;

        /**
         * Closes the room at launch: a late joiner is rejected with reason 3
         * and the session description carries the join-disabled flag (BT-242,
         * BT-244). A host with nothing to close ignores it.
         */
        virtual void setAdmissionClosed(bool closed) { (void)closed; }
    };

    /** DirectPlay's DPSESSION_JOINDISABLED, which the original sets once a game has started. */
    inline constexpr std::uint32_t TaSessionJoinDisabled = 0x20;

    /** The session advertises this many players and the host is one of them. */
    inline constexpr std::size_t TaSessionMaxPlayers = 10;

    struct TaHostConfig
    {
        TaHostPorts ports;

        /** The game's name, padded to 16 characters in the session name. */
        std::string gameName{"rwe"};

        /** Appended to the padded game name to make the session's name. */
        std::string mapName;

        /** The host's options byte, in the session description's dwUser3. */
        std::uint8_t options{0x47};

        /**
         * How many joiners are taken. The session advertises ten players and
         * the host is one, so nine is the most; a game that can only play one
         * peer says so here and the rest are turned away with a rejection.
         */
        std::size_t maxJoiners{TaSessionMaxPlayers - 1};

        /**
         * How long a joined peer may say nothing before it is rejected (reason
         * 6) and removed as if it had left. The original prompts after its
         * timeout setting, 30 s by default, and rejects on its own at that
         * setting plus 120 s (BT-233); with nobody to ask, RWE takes the
         * second figure.
         */
        std::chrono::milliseconds silenceTimeout{150000};

        /** How often the silence is looked at. */
        std::chrono::milliseconds silenceCheckInterval{1000};

        /**
         * The host's named player: the "from" of its application data and the
         * player a joiner lists. A real host's is one below its system player.
         */
        std::uint32_t hostPlayerId{0x08D90E76};
        std::uint32_t hostSystemPlayerId{0x08D90E77};

        /**
         * The id the session description carries and a joiner's
         * ADDFORWARDREQUEST addresses: DirectPlay's own name for the host,
         * distinct from both of its players.
         */
        std::uint32_t sessionHostId{0x08D80E77};

        /** The first ids handed out; each joiner after takes the pair two below. */
        std::uint32_t firstAssignedPlayerId{0x08D90E74};
        std::uint32_t firstAssignedSystemPlayerId{0x08D90E75};

        /**
         * Random per session: the original never writes one and DirectPlay
         * makes it when the session opens, so two sessions, or a restarted
         * one, are distinct (BT-208). The captured constant is not reused.
         */
        std::array<std::uint8_t, 16> instanceGuid{taRandomInstanceGuid()};

        /** How long a handshake may sit in flight before it is swept away. */
        std::chrono::milliseconds pendingHandshakeTimeout{30000};

        /** How long an accepted connection may be silent before it is closed. */
        std::chrono::milliseconds incomingIdleTimeout{10000};
    };

    class TaHostSession : public TaBattleroomHost
    {
    public:
        using PeerId = std::uint32_t;
        using PeerJoinedHandler = std::function<void(PeerId)>;
        using PeerLeftHandler = std::function<void(PeerId)>;
        using AppDataHandler = std::function<void(PeerId, const std::vector<std::uint8_t>&, TaTransport)>;

        /**
         * The session advertises maxPlayers = 10 and the host counts as one, so
         * at most this many joiners are accepted.
         */
        static constexpr std::size_t MaxPlayers = TaSessionMaxPlayers;

        /**
         * The caps on the state a peer can grow before it has joined. A
         * handshake key is a reply address the sender chooses, so the map of
         * in-flight handshakes is bounded and its entries expire; the same for
         * accepted connections, each of which holds a 64 KB read buffer.
         */
        static constexpr std::size_t MaxPendingHandshakes = 16;
        static constexpr std::size_t MaxIncomingConnections = 32;

        /** What the host is currently holding, against the caps above. */
        struct Load
        {
            std::size_t peers{0};
            std::size_t pendingHandshakes{0};
            std::size_t incomingConnections{0};
        };

        Load load() const;

        /**
         * Messages dropped because they claimed to be from a peer and came from
         * somewhere else: application data stamped with another player's id, a
         * DELETEPLAYER for a player its sender is not, a CREATEPLAYER taking
         * over a joined player's id. A pure counter, for a log and a test.
         */
        std::uint64_t forgedMessages() const { return forged; }

        /** Peers rejected and removed for saying nothing for `silenceTimeout`. */
        std::uint64_t silentPeersDropped() const { return silentDropped; }

        TaHostSession(asio::io_context& ioContext, TaHostConfig config);
        ~TaHostSession();

        TaHostSession(const TaHostSession&) = delete;
        TaHostSession& operator=(const TaHostSession&) = delete;

        /** Bind the four sockets. The ports are fixed once this returns. */
        void start();

        /** Stop listening and close every connection. Safe to call twice. */
        void stop();

        /** The ports actually bound, including any the OS chose for a zero port. */
        const TaHostPorts& localPorts() const { return ports; }

        void onPeerJoined(PeerJoinedHandler handler);
        void onPeerLeft(PeerLeftHandler handler);
        void onAppData(AppDataHandler handler);

        /** Send application data to one peer. Returns false if the peer is unknown. */
        bool send(PeerId to, std::span<const std::uint8_t> bytes, TaTransport transport) override;

        /** Send application data to every peer that has finished the handshake. */
        void sendToAll(std::span<const std::uint8_t> bytes, TaTransport transport);

        /** Send SESSIONDESCCHANGED now; the host does this whenever an option changes. */
        void sendSessionDescChanged() override;

        void setAdmissionClosed(bool closed) override;

        bool admissionClosed() const { return closed; }

        /** Drop a peer, telling the remaining peers it left. */
        void deletePeer(PeerId peerId);

        /**
         * The host leaving: DELETEPLAYER for both of its own ids, to every
         * peer. The mirror of what a joiner sends when it goes, which is how
         * a peer is told a player has left (docs/TA-NETWORK.md, "DirectPlay").
         */
        void sendHostDeleted(std::uint32_t playerId, std::uint32_t systemPlayerId);

        /** Change the options byte; the next SESSIONDESCCHANGED carries it. */
        void setOptions(std::uint8_t options);

        std::uint8_t options() const { return config.options; }

        std::uint32_t hostPlayerId() const override { return config.hostPlayerId; }

        std::optional<TaPeerAddress> peerAddress(PeerId peerId) const override;

        std::string peerName(PeerId peerId) const override;

    private:
        struct TcpLink
        {
            asio::ip::tcp::socket socket;
            std::deque<std::shared_ptr<const std::vector<std::uint8_t>>> queue;
            bool connected{false};
            bool writing{false};

            explicit TcpLink(asio::io_context& io) : socket(io) {}
        };

        struct IncomingTcp
        {
            asio::ip::tcp::socket socket;
            asio::steady_timer idleTimer;
            std::array<std::uint8_t, 65536> scratch{};
            std::vector<std::uint8_t> pending;

            explicit IncomingTcp(asio::io_context& io)
                : socket(io), idleTimer(io)
            {
            }
        };

        struct Peer
        {
            PeerId playerId{0};
            std::uint32_t systemPlayerId{0};
            asio::ip::tcp::endpoint tcpEndpoint;
            asio::ip::udp::endpoint udpEndpoint;
            std::string shortName;
            std::string longName;

            /**
             * Where its CREATEPLAYER came from. Application data and a
             * DELETEPLAYER are believed only from this address: the ids are
             * predictable and a datagram's source is the only thing here that
             * a sender cannot simply name.
             */
            asio::ip::address sourceIp;

            std::chrono::steady_clock::time_point lastHeard;
        };

        struct PendingHandshake
        {
            asio::ip::address sourceIp;
            std::uint32_t systemPlayerId{0};
            asio::ip::tcp::endpoint tcpEndpoint;
            asio::ip::udp::endpoint udpEndpoint;
            std::string name;
            std::chrono::steady_clock::time_point started;
        };

        asio::io_context& ioContext;
        TaHostConfig config;
        TaHostPorts ports;

        asio::ip::tcp::acceptor enumTcpAcceptor;
        asio::ip::tcp::acceptor gameTcpAcceptor;
        asio::ip::udp::socket enumUdpSocket;
        asio::ip::udp::socket gameUdpSocket;

        std::map<PeerId, Peer> peers;
        std::map<std::string, PendingHandshake> pending;
        std::map<std::string, std::shared_ptr<TcpLink>> outbound;

        /** Every accepted connection, so the count can be capped and each armed. */
        std::vector<std::shared_ptr<IncomingTcp>> incoming;

        /** Sweeps handshakes that never reached CREATEPLAYER. */
        asio::steady_timer pruneTimer;

        /** Looks for peers that have gone quiet. */
        asio::steady_timer silenceTimer;

        std::array<std::uint8_t, 65536> udpBuffer{};

        std::uint32_t nextPlayerId{0};
        std::uint32_t nextSystemPlayerId{0};

        bool started{false};
        bool closed{false};
        std::uint64_t forged{0};
        std::uint64_t silentDropped{0};

        PeerJoinedHandler peerJoinedHandler;
        PeerLeftHandler peerLeftHandler;
        AppDataHandler appDataHandler;

        void listenEnumTcp();
        void listenGameTcp();
        void listenEnumUdp();
        void listenGameUdp();

        /** Admit an accepted connection, or close it if the host is full. */
        void acceptIncoming(std::shared_ptr<IncomingTcp> connection);

        /** Arm the idle timer that closes a connection which has gone quiet. */
        void armIdleTimeout(std::shared_ptr<IncomingTcp> connection);

        void forgetIncoming(const std::shared_ptr<IncomingTcp>& connection);

        /** Drop handshakes older than the configured timeout, then rearm. */
        void prunePending();

        /** The handshake for a reply address, created within the cap or nullptr. */
        PendingHandshake* pendingFor(const std::string& key);

        void readTcp(std::shared_ptr<IncomingTcp> connection);
        void handleTcpBytes(std::shared_ptr<IncomingTcp> connection, const std::uint8_t* data, std::size_t len);
        void handleUdp(const std::uint8_t* data, std::size_t len, const asio::ip::udp::endpoint& source);

        void handleSystemMessage(const std::vector<std::uint8_t>& bytes, const asio::ip::address& sourceIp);
        void handleAppData(const std::uint8_t* data, std::size_t len, bool tcp, const asio::ip::address& sourceIp);

        /** Arms the look for silent peers, and the look itself. */
        void armSilenceCheck();
        void dropSilentPeers();

        /**
         * Turns a joiner away: a 0x1b naming it with the reason, down a
         * connection to the address it gave. A joiner that is not yet a peer
         * has no other way to be told, and left unanswered it waits for ever.
         */
        void rejectJoiner(
            std::uint32_t playerId,
            const asio::ip::tcp::endpoint& tcpEndpoint,
            std::uint8_t reason);

        void handleEnumSessions(const asio::ip::tcp::endpoint& reply);
        void handleRequestPlayerId(std::span<const std::uint8_t> payload, const asio::ip::tcp::endpoint& reply);
        void handleAddForwardRequest(std::span<const std::uint8_t> payload, const asio::ip::tcp::endpoint& reply);
        void handleCreatePlayer(std::span<const std::uint8_t> payload, const asio::ip::tcp::endpoint& reply);
        void handleDeletePlayer(std::span<const std::uint8_t> payload, const asio::ip::address& sourceIp);

        /**
         * The outbound connection to a reply address, opened on first use. All
         * the system replies and all outgoing TCP application data for a peer
         * travel down this one connection, because every socket receives only.
         */
        std::shared_ptr<TcpLink> linkFor(const asio::ip::tcp::endpoint& endpoint);
        void enqueue(std::shared_ptr<TcpLink> link, std::vector<std::uint8_t> bytes, const std::string& key);
        void writeNext(const std::shared_ptr<TcpLink>& link, const std::string& key);

        void sendSystemTo(const asio::ip::tcp::endpoint& endpoint, TaDirectPlayCommand command, std::span<const std::uint8_t> payload);

        TaSessionDescription makeSessionDescription() const;
        TaSuperEnumPlayersReply makeSuperEnum(const TaSuperPackedPlayer* extraSystemPlayer) const;

        asio::ip::tcp::endpoint resolveReplyEndpoint(
            const TaDpAddress& address,
            const asio::ip::address& sourceIp,
            std::uint16_t fallbackPort) const;

        static std::vector<std::uint8_t> addressIp(const asio::ip::address& address);
    };
}
