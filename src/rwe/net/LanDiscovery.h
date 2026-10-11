#pragma once

#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <rwe/rwe_time.h>
#include <string>
#include <thread>
#include <utility>
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
        /** Starting metal for this seat. Clamped to 0..1000000 on decode. */
        int metal{1000};
        /** Starting energy for this seat. Clamped to 0..1000000 on decode. */
        int energy{1000};
        /** The computer player's personality, at most 32 characters. May be empty. */
        std::string personality{};
        /**
         * Whether the person in this seat has pressed Ready. A guest sets it
         * on its own claim announcement; the host mirrors every seat's flag
         * in its own. Wire key `slotr`, which is only written for a seat
         * that is ready, so a lobby nobody is ready in costs nothing.
         */
        bool ready{false};
    };

    /**
     * One line of a lobby's chat, as it travels in an announcement.
     *
     * Chat rides on discovery because it is the one channel a lobby already
     * has: every machine in the room hears the host's announcement, and the
     * host hears each guest's claim. That makes it a *tail* and not a log --
     * at most LanDiscovery::maxChatLines lines, each one repeated in every
     * announcement for as long as it stays in the tail -- so a reader tells a
     * line it has already shown from a new one by its sequence number.
     */
    struct DiscoveredChatLine
    {
        /** Per speaker, counts up from 1; lets a reader tell a line it has seen from a new one. */
        unsigned int sequence{0};
        /** Who said it, at most maxHostNameLength bytes. Empty for a system line ("X has joined"). */
        std::string name;
        /** The text, at most maxChatTextLength bytes, no control characters. */
        std::string text;
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

        /**
         * The display name of the person hosting, at most 20 bytes. May be
         * empty. Wire key `host`.
         */
        std::string hostName{};

        /**
         * The password gate: empty for none, otherwise exactly 16 lower-case
         * hex characters as LanDiscovery::hashPassword produces them. Wire
         * key `pw`. See hashPassword for what this is, and is not, worth.
         */
        std::string passwordHash{};

        /**
         * Set by the host for the couple of seconds after it presses Start,
         * so that guests mirroring the lobby can start as well. Wire key
         * `launch`, which is `1` or absent.
         */
        bool launching{false};

        /**
         * Game-wide options, in the order the host listed them. Every peer
         * has to agree on these or the lockstep simulation desyncs, which is
         * why they travel with the lobby and not as something each machine
         * is trusted to set the same by hand. The codec does not know what
         * any of them mean -- that is the UI layer's business -- only that a
         * name is 1..32 characters of [A-Za-z0-9_], a value is at most 64
         * characters, there are at most 32 of them and a repeated name keeps
         * its first. Wire keys `opt.<name>`.
         */
        std::vector<std::pair<std::string, std::string>> options{};

        /**
         * Chat, oldest first, at most LanDiscovery::maxChatLines lines. On a
         * host's announcement it is the tail of the room's transcript, each
         * line carrying the host's own sequence number, which is unique and
         * increasing across the room. On a guest's claim announcement it is
         * that guest's most recent line or lines that it wants said, and the
         * host de-duplicates them by (session, sequence). Wire key `chat`,
         * one per line.
         *
         * Chat is the first thing to give way when an announcement is too
         * big -- see LanDiscovery::encode -- so a reader must not count on a
         * line being present, only on the lines that are present being whole.
         */
        std::vector<DiscoveredChatLine> chat{};

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
     *
     * The format grows only by adding keys, and the version line stays
     * `RWE-DISCOVERY/1`: a decoder ignores a key it does not know, so an
     * older build still reads a newer announcement and a newer build still
     * reads an older one, with every new field at its default. That is why
     * a seat's extras (metal, energy, personality) are a `slotx=` line of
     * their own and not more fields on the `slot=` line, which an older
     * decoder checks for exactly seven and would drop whole -- and why a
     * seat's ready flag is a `slotr=` line of its own and not a token added
     * to `slotx=`, which an older decoder checks for exactly four.
     */
    class LanDiscovery
    {
    public:
        static constexpr unsigned short defaultPort = 1338;
        static constexpr std::size_t maxMessageSize = 1400;
        /** How often a lobby is re-announced. Short enough to feel live. */
        static constexpr std::chrono::milliseconds announceInterval{1000};
        /**
         * The shortest gap between two announcements that were sent because
         * the lobby changed, and not because an interval had gone by. See
         * advertise().
         */
        static constexpr std::chrono::milliseconds changeAnnounceGap{100};
        /**
         * How long an announcement is remembered after the last one. Three
         * missed announcements is a machine that has gone away, or a
         * firewall that has just started dropping them.
         */
        static constexpr std::chrono::milliseconds expiry{3500};
        /**
         * The shortest gap between two replies to one machine's probes.
         * Anything that sends faster than this is not a person typing an
         * address, and a reply is far larger than the probe that asked for it.
         */
        static constexpr std::chrono::milliseconds probeReplyInterval{250};
        /** How long what a host name resolved to is believed. */
        static constexpr std::chrono::milliseconds nameCacheTime{60000};
        /** How soon a host name that did not resolve is looked up again. */
        static constexpr std::chrono::milliseconds nameRetryInterval{5000};
        /** Host names being looked up at the same moment, at most. */
        static constexpr std::size_t maxPendingNameLookups = 8;

        /** What an option can hold, as the codec enforces it. */
        static constexpr std::size_t maxOptions = 32;
        static constexpr std::size_t maxOptionNameLength = 32;
        static constexpr std::size_t maxOptionValueLength = 64;
        static constexpr std::size_t maxHostNameLength = 20;
        static constexpr std::size_t maxPersonalityLength = 32;
        static constexpr int maxStartingResource = 1000000;
        /** What the chat tail can hold, as the codec enforces it. */
        static constexpr std::size_t maxChatLines = 8;
        static constexpr std::size_t maxChatTextLength = 80;

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
         * How often a lobby is re-announced when nothing has changed. The
         * default is announceInterval. It is read each time the next
         * announcement is armed, so a call before start() governs the very
         * first gap, and one made later takes effect after the announcement
         * already waiting. A short interval is a way to make a lobby feel
         * live and a long one is a way for a test to say that something
         * arrived because of a change and not because a timer went off.
         */
        void setAnnounceInterval(std::chrono::milliseconds newInterval);

        /**
         * Advertises a lobby until stop(). Safe to call before start(); it
         * does nothing until there is a socket.
         *
         * A lobby that is different from the one advertised before it -- as
         * encode() would put it on the wire, so a change the codec drops
         * anyway is not a change -- is announced at once, to the broadcast
         * addresses and every announce target, and not at the next
         * announceInterval. A chat line or a ready toggle that waited up to
         * a second to reach anybody would read as broken. Advertising the
         * same lobby again sends nothing: it is the *change* that goes out,
         * and the regular announcements carry on as before.
         *
         * Changes are sent at most changeAnnounceGap apart, ten a second. A
         * change that arrives inside that window is not lost and not
         * queued: the one announcement that goes out when the window ends
         * carries the latest lobby, however many changes there were, and
         * if a regular announcement happens first it is that one that
         * carries it. The send happens on the network thread, so this call
         * costs a lock and a timer and never a socket send.
         */
        void advertise(const DiscoveredGame& game);
        void stopAdvertising();

        /**
         * Machines each announcement is sent to by address, as well as being
         * broadcast: the other half of probe(). A broadcast stays on this
         * subnet, so a guest that found its host by typing an address
         * (another subnet, a VPN) would never be heard by it, and a game
         * needs the host to hear the guest. Each entry is written as probe()
         * takes it -- `a.b.c.d[:port]`, `[v6][:port]`, `name[:port]` -- and
         * one that does not parse is skipped. The list replaces the last
         * one; an empty list is the default of broadcasting only.
         *
         * A name is looked up on the network side, the way probe() does it,
         * and is announced to from the moment it has resolved: until then
         * the entry is in the list and sends nothing. The announcement that
         * was waiting for it goes out as soon as the lookup lands.
         */
        void setAnnounceTargets(const std::vector<std::string>& hostsAndPorts);

        /**
         * Sessions currently advertising, newest announcement last. This
         * includes this machine's own lobby, which is delivered to loopback
         * like any other; use the overload below to leave it out.
         */
        std::vector<DiscoveredGame> games() const;

        /** As games(), without any entry whose session is `excludeSession`. */
        std::vector<DiscoveredGame> games(const std::string& excludeSession) const;

        /**
         * Asks one machine, by address or by name, whether it is advertising
         * a lobby: the "type an IP address instead of scanning" path, for a
         * network where broadcasts do not cross (a different subnet, a VPN,
         * a firewall that eats 255.255.255.255).
         *
         * `hostAndPort` is an IPv4 address (`192.168.2.120`), an IPv6 one
         * (`::1`, `[fe80::1]`), a host name (`my-pc`, `my-pc.lan`), or any
         * of them with a port (`192.168.2.120:1338`, `[::1]:1338`,
         * `my-pc:1338`). Without a port it is `defaultPort`. The port is the
         * *discovery* port the other machine started on, not the game port.
         *
         * What an address sends is one short datagram, so it returns as soon
         * as that is handed to the socket: true if it was, false if the
         * address did not parse, there was no socket for that address family
         * (this machine could not bind IPv6, say), or the send failed.
         *
         * A name is never looked up on the caller's thread, because that
         * blocks on a DNS server for as long as it cares to take. It is
         * handed to the resolver, which asio runs on a helper thread of its
         * own, and the answer is kept for nameCacheTime, preferring an IPv4
         * result where there are both. The call returns true once the
         * lookup is accepted -- or the name is already known and the probe
         * was sent, or its failure is still being remembered. Until the
         * name has resolved nothing is sent; the probe is made once, to the
         * port that was asked for, when it lands. A lookup that fails is
         * remembered and tried again no sooner than nameRetryInterval, so a
         * list that probes once a second does not hammer a resolver that has
         * already said no. At most maxPendingNameLookups names are being
         * looked up at once, and a request for another is refused with
         * false. There is no result to wait for here: when a machine
         * answers, it arrives like any announcement.
         *
         * Nothing comes back on this call. A machine that is advertising
         * answers the probe with its normal announcement, unicast to this
         * machine's discovery socket, and it arrives through the same path
         * as any other: it appears in games() with `sourceAddress` set to
         * the replier. A machine that is not advertising does not answer.
         *
         * Callable at any time after start(), and as often as wanted -- a
         * list that is showing can call it once a second -- since the
         * answering side rate limits itself.
         */
        bool probe(const std::string& hostAndPort);

        /**
         * Whether probe() would take this text as an address or a host
         * name: the same parser, with no socket and no lookup. The TCP
         * screen asks before it leaves, so that "Invalid TCP/IP Address" is
         * said at the dialog and not discovered a second later as a list
         * that never fills. A blank string is not an address -- the screen
         * treats blank as "scan" and never asks.
         *
         * A name is accepted for how it is written and not for whether it
         * exists: 1..253 characters in dot-separated labels of 1..63 of
         * [A-Za-z0-9-], none starting or ending with a hyphen, and at least
         * one letter somewhere, which is what keeps `999.1.1.1` an invalid
         * address and not a very odd name.
         */
        static bool isProbeAddress(const std::string& hostAndPort);

        /**
         * The password gate's hash: FNV-1a, 64 bit, over
         * `session + "\n" + password`, as 16 lower-case hex characters.
         *
         * **This is a courtesy, not security.** It exists so that somebody
         * does not wander into a game that was meant to be private to a
         * group, or join "Bob's game" when they meant "Alice's game". The
         * hash goes out in every announcement, visible to anyone on the
         * LAN, FNV is not a password hash and a short password falls to a
         * dictionary in no time, and the game traffic itself carries no
         * authentication at all: anyone who knows a host's address can
         * connect to it whatever this says. Do not describe it to a player
         * as protection.
         *
         * The session is part of the input so that two lobbies using the
         * same password do not advertise the same hash, which would
         * otherwise read as "these are the same password" off the wire.
         */
        static std::string hashPassword(const std::string& session, const std::string& password);

        /**
         * Encodes and decodes the wire format. Public because the codec is
         * the part worth testing exhaustively, and worth being able to run
         * against a captured datagram without starting a socket.
         *
         * encode() returns nothing for a lobby it could not put on the wire:
         * no session, a field over its bound, or text that will not fit in
         * maxMessageSize. When it will not fit, things give way in a fixed
         * order, from what a guest can most do without to what it cannot:
         *
         *   1. chat lines, whole ones, oldest first -- a line is either
         *      there complete or not there, and the newest survive;
         *   2. options, the trailing ones, one at a time;
         *   3. and only if it still does not fit with neither is the lobby
         *      refused.
         *
         * Everything else a lobby says is needed to join it; chat and
         * options are the parts a guest can do without being unable to join.
         *
         * A chat line the codec would not accept back -- no text, a
         * sequence outside 1..2147483647, a name or text over its bound, a
         * control character in either -- is left out and does not fail the
         * lobby, because a fault in somebody's chat must never be the
         * reason a lobby stops being advertised. More than maxChatLines
         * keeps the newest maxChatLines.
         */
        static std::optional<std::string> encode(const DiscoveredGame& game);
        static std::optional<DiscoveredGame> decode(const std::string& text);

        /** The datagram probe() sends. Public so a test can send one by hand. */
        static const std::string& probeMessage();

    private:
        /** An entry of setAnnounceTargets: an address, or a name to resolve. */
        struct AnnounceTarget
        {
            bool isName{false};
            asio::ip::udp::endpoint endpoint;
            std::string name;
            unsigned short port{0};
        };

        /** What a host name is known to resolve to, or is waiting to. */
        struct NameLookup
        {
            /** Lower case: names do not care. */
            std::string name;
            bool pending{false};
            bool hasAddress{false};
            asio::ip::address address;
            Timestamp addressAt{};
            bool failed{false};
            Timestamp failedAt{};
            /** Probes to send when the lookup lands. Bounded, and de-duplicated. */
            std::vector<unsigned short> probePorts;
        };

        /**
         * Binds the socket, then serves until stop(). Reports whether the bind
         * worked the moment that is known -- as soon as the socket is open
         * and while there is still no serving to do -- because start() waits
         * on that answer and the serving itself does not end until stop().
         */
        void bindAndServe(unsigned short port, std::shared_ptr<std::promise<bool>> bound);
        /** Starts a receive on each socket there is. */
        void listen();
        /** Starts a receive on the IPv4 socket, or on the IPv6 one. */
        void listenOn(bool v6);
        /** What to do with the datagram that has just arrived. Network thread only. */
        void handleDatagram(bool v6, std::size_t bytes);
        /** Replies to a probe with this machine's announcement, if it has one. */
        void answerProbe(bool v6, const asio::ip::udp::endpoint& sender);
        /** Announces, then arms the timer for the next one. Network thread only. */
        void announceLoop();
        /** Sends the advertised lobby to every place it goes, once. Network thread only. */
        void announceNow();
        /** Arms the send that advertise() asked for; the mutex is held. */
        void scheduleChangeAnnouncement();
        void prune();
        std::vector<std::string> broadcastAddresses() const;

        /**
         * Hands one datagram to whichever socket suits the destination's
         * family, turning a v4-mapped address into the plain one for the
         * reason probe() gives. The mutex is held. False if there is no
         * socket for the family or the send failed.
         */
        bool sendDatagram(const std::string& data, asio::ip::udp::endpoint destination);

        /**
         * What `name` resolves to, if that is known and still believed,
         * starting a lookup if one is due. `probePort`, if not zero, is a
         * probe to make when the lookup lands. `accepted` is false only when
         * the name is new and there are already maxPendingNameLookups under
         * way. The mutex is held.
         */
        struct NameAnswer
        {
            std::optional<asio::ip::address> address;
            bool accepted{true};
        };
        NameAnswer lookUpName(const std::string& name, unsigned short probePort);
        void startLookup(NameLookup& entry);
        void onNameResolved(
            const std::string& name,
            unsigned long long generation,
            const asio::error_code& error,
            const asio::ip::udp::resolver::results_type& results);
        std::size_t pendingLookups() const;
        /** The destinations of the announce targets that can be reached now. The mutex is held. */
        std::vector<asio::ip::udp::endpoint> reachableAnnounceTargets();

        mutable std::mutex mutex;
        std::thread networkThread;
        /** Set on the network thread once the bind has been attempted. */
        std::atomic<bool> bound{false};
        /**
         * The IPv4 socket: it announces, broadcasts and is what isRunning()
         * answers for. A second, IPv6-only socket on the same port exists
         * for probes and their replies, because a socket is one address
         * family on Windows and an IPv4 address sent to from an IPv6 socket
         * is the fault EndpointFamily.h describes. Keeping them apart means
         * no v4-mapped address ever has to be built here.
         */
        std::optional<asio::ip::udp::socket> socket;
        std::optional<asio::ip::udp::socket> socket6;
        asio::io_context ioContext;

        /**
         * Counts runs: bumped by every stop(). A timer or a lookup that was
         * armed in an earlier run is still in the io_context's queue when
         * it is restarted and would otherwise fire into the next one --
         * which, for the announce timer, is a second announce loop for
         * every start() after the first.
         */
        unsigned long long generation{0};

        /** What to advertise, if anything. Read by the announce thread. */
        std::optional<DiscoveredGame> advertised;
        /** advertised, as encode() writes it; what a new lobby is compared with. */
        std::optional<std::string> advertisedText;
        /** A change has been advertised that no announcement has carried yet. */
        bool unsentChange{false};
        /** A change announcement is armed, and will carry whatever is latest. */
        bool changeAnnouncementArmed{false};
        std::chrono::steady_clock::time_point lastChangeAnnouncement{};

        /** Where else to send it; see setAnnounceTargets. */
        std::vector<AnnounceTarget> announceTargets;
        std::chrono::milliseconds interval{announceInterval};

        /** Session id -> what was heard. Ordered by first sight. */
        std::vector<DiscoveredGame> heard;

        /** Host names asked about, newest last. Bounded; see lookUpName. */
        std::vector<NameLookup> nameLookups;

        /**
         * Who was last answered a probe and when. Bounded, and entries
         * older than probeReplyInterval carry no information and are
         * dropped as it is consulted.
         */
        std::vector<std::pair<std::string, Timestamp>> recentProbeReplies;

        /**
         * Owned here rather than in listen(): the receive buffer and the
         * sender's address have to outlive the call that started the
         * operation, because asio runs the handler after listen() has
         * returned, and a handler holding a reference to a local is a
         * dangling one. Both are touched only by the network thread.
         */
        std::array<char, 2048> receiveBuffer{};
        asio::ip::udp::endpoint currentSender;
        std::array<char, 2048> receiveBuffer6{};
        asio::ip::udp::endpoint currentSender6;
    };
}
