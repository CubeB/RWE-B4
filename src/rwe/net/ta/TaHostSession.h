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

    struct TaHostConfig
    {
        TaHostPorts ports;

        /** The game's name, padded to 16 characters in the session name. */
        std::string gameName{"rwe"};

        /** Appended to the padded game name to make the session's name. */
        std::string mapName;

        /** The host's options byte, in the session description's dwUser3. */
        std::uint8_t options{0x4F};

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

        std::array<std::uint8_t, 16> instanceGuid{
            0x88, 0xFE, 0xCB, 0xC9, 0xEF, 0xDB, 0x94, 0x43,
            0xB9, 0x5C, 0x0D, 0x53, 0xCC, 0x2C, 0x18, 0xD4};

        /** How long a handshake may sit in flight before it is swept away. */
        std::chrono::milliseconds pendingHandshakeTimeout{30000};

        /** How long an accepted connection may be silent before it is closed. */
        std::chrono::milliseconds incomingIdleTimeout{10000};
    };

    class TaHostSession
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
        static constexpr std::size_t MaxPlayers = 10;

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
        bool send(PeerId to, std::span<const std::uint8_t> bytes, TaTransport transport);

        /** Send application data to every peer that has finished the handshake. */
        void sendToAll(std::span<const std::uint8_t> bytes, TaTransport transport);

        /** Send SESSIONDESCCHANGED now; the host does this whenever an option changes. */
        void sendSessionDescChanged();

        /** Drop a peer, telling the remaining peers it left. */
        void deletePeer(PeerId peerId);

        /** Change the options byte; the next SESSIONDESCCHANGED carries it. */
        void setOptions(std::uint8_t options);

        std::uint8_t options() const { return config.options; }

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
        };

        struct PendingHandshake
        {
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

        std::array<std::uint8_t, 65536> udpBuffer{};

        std::uint32_t nextPlayerId{0};
        std::uint32_t nextSystemPlayerId{0};

        bool started{false};

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
        void handleAppData(const std::uint8_t* data, std::size_t len, bool tcp);

        void handleEnumSessions(const asio::ip::tcp::endpoint& reply);
        void handleRequestPlayerId(std::span<const std::uint8_t> payload, const asio::ip::tcp::endpoint& reply);
        void handleAddForwardRequest(std::span<const std::uint8_t> payload, const asio::ip::tcp::endpoint& reply);
        void handleCreatePlayer(std::span<const std::uint8_t> payload, const asio::ip::tcp::endpoint& reply);
        void handleDeletePlayer(std::span<const std::uint8_t> payload);

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
