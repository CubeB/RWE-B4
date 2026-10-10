#pragma once

#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <rwe/rwe_time.h>
#include <string>
#include <thread>
#include <vector>

namespace rwe
{
    /**
     * One seat of a lobby, as another machine was told about it.
     *
     * This is deliberately not MainMenuModel::PlayerSettings. What is on the
     * wire is a description somebody else wrote and this machine has to
     * survive parsing, so it is plain data with no UI concerns: a name is a
     * bounded string, a colour is a number that was range checked on the way
     * in, and a controller is one of four known words rather than an enum
     * value cast from a network octet.
     */
    struct DiscoveredSlot
    {
        int index{-1};
        std::string controller{"open"};
        std::string name;
        std::string side{"ARM"};
        unsigned int colour{0};
        int team{-1};
        /** Where the machine playing this seat is, host:port. May be empty. */
        std::string address;
    };

    /**
     * A lobby somebody on this LAN is advertising.
     *
     * It carries the whole slot list, every address included, and not just
     * the announcer's own seat. That is what lets a machine that has never
     * spoken to the other two join a three-player game: it mirrors the
     * announcer's list and knows all three addresses at once. An
     * announcement carrying only its own address would only ever be enough
     * for two players.
     */
    struct DiscoveredGame
    {
        std::string session;
        std::string map;
        unsigned int port{1337};
        std::string label;
        std::vector<DiscoveredSlot> slots;
        /** Where the announcement came from, host:port. Not part of the wire. */
        std::string sourceAddress;
        Timestamp lastSeen{};
    };

    /**
     * Finds RWE lobbies on the local network.
     *
     * RWE has no discovery of any kind: every machine has to be told every
     * other machine's address, on every peer, identically. The setup screen
     * says so -- "Everyone sets this screen the same way, with their own
     * seat as Player" -- but a screen you have to fill in identically from
     * two machines is a screen people get wrong, and the failure is silent
     * enough that a mismatch reads as a network fault.
     *
     * This listens on one fixed port for announcements other machines make
     * while they sit on the multiplayer screen, and remembers what it has
     * heard for as long as the announcements keep coming. Nothing here
     * reaches the simulation: it decides what the setup screen starts out
     * showing, and a game that is actually started is still agreed the way
     * it always was.
     *
     * The wire format is text, one field per line, and every variable-length
     * value percent-encoded. That is a deliberate choice against protobuf,
     * which is already a dependency: an announcement is at most one small
     * datagram, sent once a second, never persisted and never forwarded, so
     * a build-time codegen step and a change to the protocol file would buy
     * nothing that bounds-checked parsing does not already give. It also
     * means an announcement is readable in a packet capture, which is worth
     * a great deal on a LAN bug like the one that made this worth writing.
     */
    class LanDiscovery
    {
    public:
        static constexpr unsigned short defaultPort = 1338;
        static constexpr std::size_t maxMessageSize = 1400;
        /** How often a lobby is re-announced. Short enough to feel live. */
        static constexpr std::chrono::milliseconds announceInterval{1000};
        /**
         * How long an announcement is remembered after the last one. Three
         * missed announcements is a machine that has gone away, or a
         * firewall that has just started dropping them.
         */
        static constexpr std::chrono::milliseconds expiry{3500};

        LanDiscovery();
        ~LanDiscovery();

        /**
         * Starts listening and announcing. Returns once the socket is
         * actually bound, or has been found to be unbindable -- never
         * before. A promise is used rather than a flag so that a caller can
         * ask isRunning() immediately afterwards and be told the truth: with
         * a flag set on the network thread there is a window in which
         * start() has returned and the socket does not exist yet, which
         * reads as "discovery failed" for a service that is merely still
         * starting up.
         */
        void start(unsigned short port);
        void stop();

        /**
         * Whether the socket was bound. Only meaningful once start() has
         * returned. A failure to bind is not fatal to a game -- it means
         * this machine cannot discover or be discovered, and a game started
         * by typing addresses still works.
         */
        bool isRunning() const;

        /**
         * Advertises a lobby until stop(). Safe to call before start(); it
         * does nothing until there is a socket.
         */
        void advertise(const DiscoveredGame& game);
        void stopAdvertising();

        /** Sessions currently advertising, newest announcement last. */
        std::vector<DiscoveredGame> games() const;

        /**
         * Encodes and decodes the wire format. Public because the codec is
         * the part worth testing exhaustively, and worth being able to run
         * against a captured datagram without starting a socket.
         */
        static std::optional<std::string> encode(const DiscoveredGame& game);
        static std::optional<DiscoveredGame> decode(const std::string& text);

    private:
        /**
         * Binds the socket, then serves until stop(). Reports whether the bind
         * worked the moment that is known -- as soon as the socket is open
         * and while there is still no serving to do -- because start() waits
         * on that answer and the serving itself does not end until stop().
         */
        void bindAndServe(unsigned short port, std::shared_ptr<std::promise<bool>> bound);
        void listen();
        void announceLoop();
        void prune();
        std::vector<std::string> broadcastAddresses() const;

        mutable std::mutex mutex;
        std::thread networkThread;
        /** Set on the network thread once the bind has been attempted. */
        std::atomic<bool> bound{false};
        std::optional<asio::ip::udp::socket> socket;
        asio::io_context ioContext;

        /** What to advertise, if anything. Read by the announce thread. */
        std::optional<DiscoveredGame> advertised;
        std::chrono::milliseconds interval{announceInterval};

        /** Session id -> what was heard. Ordered by first sight. */
        std::vector<DiscoveredGame> heard;

        /**
         * Owned here rather than in listen(): the receive buffer and the
         * sender's address have to outlive the call that started the
         * operation, because asio runs the handler after listen() has
         * returned, and a handler holding a reference to a local is a
         * dangling one. Both are touched only by the network thread.
         */
        std::array<char, 2048> receiveBuffer{};
        asio::ip::udp::endpoint currentSender;
    };
}