#pragma once

#include <array>
#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <rwe/rwe_time.h>
#include <string>
#include <vector>

namespace rwe::stun
{
    /**
     * What a STUN server is used for here, and the whole of it: it is asked
     * "what address did this datagram come from?", and answers. Nothing
     * else is asked and nothing is relayed. A router that sits between this
     * machine and the internet rewrites the source of every datagram, so the
     * address and port a machine has are not the ones anybody else can send
     * to; this is how it finds out the ones they can.
     *
     * The servers are public, free and run by other people (RFC 5389), so
     * the list below is a list to try in order and not a dependency: one
     * being down costs a second, and all of them being down costs the
     * ability to be found from outside a LAN and nothing else.
     */
    struct Server
    {
        std::string host;
        unsigned short port{3478};
    };

    /** The servers asked, in the order they are tried. */
    const std::vector<Server>& defaultServers();

    using TransactionId = std::array<std::uint8_t, 12>;

    /** A fresh random transaction id. Safe to call from any thread. */
    TransactionId randomTransactionId();

    /** A Binding request: the 20 byte header and nothing after it. */
    std::string makeBindingRequest(const TransactionId& id);

    struct BindingResponse
    {
        TransactionId id{};
        /** The address and port the server saw the request come from. IPv4. */
        asio::ip::udp::endpoint mapped;
    };

    /**
     * Whether a datagram has the shape of a STUN message at all: long
     * enough, the top two bits clear, the magic cookie in place and a length
     * that is exactly the rest of the datagram. Cheap, and what a socket
     * shared with other traffic asks before offering a datagram to anything
     * that parses STUN.
     */
    bool looksLikeStun(const char* data, std::size_t size);

    /**
     * The mapped address of a Binding success response, or nothing for
     * anything else: a request, an error response, a message that does not
     * parse, one with no address in it, or one with an address that cannot
     * be sent to (port 0, 0.0.0.0, multicast). This reads bytes a stranger
     * chose, so every length is checked before it is used.
     *
     * XOR-MAPPED-ADDRESS is preferred. MAPPED-ADDRESS is read when it is
     * all there is, for a server that predates RFC 5389. Only IPv4 is
     * returned: the sockets this is used on are IPv4, and the invite code
     * carries an IPv4 address.
     */
    std::optional<BindingResponse> parseBindingResponse(const char* data, std::size_t size);

    /**
     * Resolves each server to an IPv4 endpoint, skipping the ones that do
     * not resolve. Blocks on DNS, so a caller keeps it off any thread that
     * has a frame to draw.
     */
    std::vector<asio::ip::udp::endpoint> resolveServers(const std::vector<Server>& servers);

    /**
     * Finds out what one socket looks like from outside.
     *
     * It does no I/O of its own. The owner hands it a way to send a
     * datagram from *the socket in question* -- the point of the exercise
     * is that the answer is about that socket's port and no other -- and
     * feeds it what arrives and the time. Not thread safe: the owner
     * serialises.
     *
     * Every server is asked at once and unanswered requests are repeated,
     * because a datagram is allowed to be lost and the first one so often
     * is: the router has no mapping for this socket yet, and the network
     * may drop the first packet of a flow while it learns one. The probe is
     * settled -- State::Done -- when every server has answered, or one has
     * and a moment has passed without another. A router that gives this
     * socket one external port whoever it talks to answers all of them with
     * the same port; one that gives a different one per destination does
     * not, and what it says is useless to anybody who is not the server, so
     * that case is told apart and named in mapping().
     */
    class ReflexiveProbe
    {
    public:
        using Send = std::function<bool(const asio::ip::udp::endpoint&, const std::string&)>;

        enum class State
        {
            Idle,
            Pending,
            Done,
            Failed
        };

        enum class Mapping
        {
            /** Fewer than two servers have answered. */
            Unknown,
            /** Every server saw the same address and port. A hole punched for one is a hole for all. */
            EndpointIndependent,
            /** The servers saw different ports. No peer can be told where to send. */
            EndpointDependent
        };

        static constexpr std::chrono::milliseconds retryInterval{250};
        static constexpr std::chrono::milliseconds settleTime{1000};
        static constexpr std::chrono::milliseconds timeout{4000};

        explicit ReflexiveProbe(Send send);

        void begin(std::vector<asio::ip::udp::endpoint> servers, Timestamp now);

        /**
         * Offers a datagram. True if it was a response to this probe's own
         * request, from a server it asked, and so consumed. False means it
         * is somebody else's and the caller should go on with it.
         */
        bool onDatagram(const char* data, std::size_t size, const asio::ip::udp::endpoint& from, Timestamp now);

        /** Repeats what is unanswered and settles or fails the probe. */
        void tick(Timestamp now);

        State state() const { return currentState; }
        Mapping mapping() const;

        /** What the servers saw: the address most of them agreed on, once any has answered. */
        std::optional<asio::ip::udp::endpoint> mapped() const;

        std::size_t answerCount() const;

    private:
        struct Query
        {
            asio::ip::udp::endpoint server;
            TransactionId id{};
            std::optional<asio::ip::udp::endpoint> answer;
        };

        void sendUnanswered();
        void settleIfDue(Timestamp now);

        Send send;
        State currentState{State::Idle};
        std::vector<Query> queries;
        Timestamp startedAt{};
        Timestamp lastSentAt{};
        Timestamp firstAnswerAt{};
    };
}
