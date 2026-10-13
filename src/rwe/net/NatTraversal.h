#pragma once

#include <asio.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <rwe/net/LanDiscovery.h>
#include <rwe/net/Stun.h>
#include <string>
#include <thread>
#include <vector>

namespace rwe
{
    /**
     * Gets this machine's two UDP sockets known to the outside, and keeps a
     * way open to the peers that were told about them.
     *
     * Behind a router a socket's address is not the one anybody else can
     * send to, and behind a carrier's router -- which is what a 4G or a
     * satellite connection is -- there is no port to forward and no one to
     * ask. Two machines in that position can still reach each other if each
     * sends to the other at the same time, because the first datagram out
     * is what makes the router expect an answer. That needs each side to
     * know the other's outside address, and each to be sending *from the
     * socket it will receive on*, so this finds the outside address of two:
     *
     *   - the discovery socket (1338), on which the lobby is carried, by
     *     asking from it through LanDiscovery, which owns it; and
     *   - the game's own port, which the lobby does not hold open and the
     *     game has not bound yet: a socket of this class's own, bound to
     *     that port for as long as the lobby is up and released just before
     *     the game binds it, so the game's first datagram leaves from the
     *     same port and the router reuses the mapping this made.
     *
     * Nothing here changes what a game does once it has started and nothing
     * here is consulted by a LAN game: it is started when a person asks for
     * an invite or joins by one, and a lobby that never does has none of it.
     *
     * Reuse of the game port's mapping rests on the router being
     * endpoint-independent -- the same outside port whoever the socket
     * talks to -- and the probe checks that and says so when it is not, in
     * Status::problem, because a code that cannot work should not be handed
     * out.
     */
    class NatTraversal
    {
    public:
        enum class Phase
        {
            Idle,
            Working,
            Ready,
            Failed
        };

        struct Status
        {
            Phase phase{Phase::Idle};
            /** The discovery socket as the internet sees it. */
            std::optional<asio::ip::udp::endpoint> discoveryAddress;
            /** The game port as the internet sees it. */
            std::optional<asio::ip::udp::endpoint> gameAddress;
            /** When Failed, a sentence for the person who has to act on it. */
            std::string problem;
        };

        /** The datagram two punching peers send each other. Public so a test can send one by hand. */
        static const std::string& punchMessage();

        /** How often a punch target is sent to. */
        static constexpr std::chrono::milliseconds punchInterval{400};

        /** Hooks `discovery`, which must outlive this and be started before start(). */
        explicit NatTraversal(LanDiscovery& discovery);
        ~NatTraversal();

        NatTraversal(const NatTraversal&) = delete;
        NatTraversal& operator=(const NatTraversal&) = delete;

        /**
         * Begins finding out, and returns at once: names are resolved and
         * the probes run on a thread of their own, and status() is how to
         * watch. `servers` that are names are looked up there too.
         */
        void start(unsigned short gamePort, const std::vector<stun::Server>& servers = stun::defaultServers());

        /** As above for servers already resolved, which is what a test with a server of its own has. */
        void start(unsigned short gamePort, std::vector<asio::ip::udp::endpoint> servers);

        /**
         * Releases the game port and stops. After this returns the port is
         * free to be bound by whoever is about to. Idempotent.
         */
        void stop();

        Status status() const;

        /**
         * Who to keep a way open to: the game addresses of the peers, as the
         * lobby was told them. Replaces the list. Each is sent a small
         * datagram every punchInterval from the game port, which is what
         * makes the router expect one back from it.
         */
        void setPunchTargets(std::vector<asio::ip::udp::endpoint> targets);

        /**
         * The addresses a punch datagram has been received from, oldest
         * first and without repeats. This is a statement that a datagram
         * got through, from that address and not from the one the lobby
         * gave if they differ -- which is what peer address learning goes
         * on to use.
         */
        std::vector<asio::ip::udp::endpoint> heardFrom() const;

    private:
        void run(unsigned short gamePort, std::vector<stun::Server> names, std::vector<asio::ip::udp::endpoint> servers);
        void refreshStatus();
        bool onDiscoveryDatagram(const char* data, std::size_t size, const asio::ip::udp::endpoint& from);

        LanDiscovery& discovery;

        mutable std::mutex mutex;
        std::thread worker;
        std::atomic<bool> stopRequested{false};

        /** Guarded by `mutex`. */
        std::unique_ptr<stun::ReflexiveProbe> discoveryProbe;
        std::unique_ptr<stun::ReflexiveProbe> gameProbe;
        Status current;
        std::vector<asio::ip::udp::endpoint> punchTargets;
        std::vector<asio::ip::udp::endpoint> heard;
        std::string bindProblem;
    };
}
