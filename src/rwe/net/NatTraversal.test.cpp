#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <rwe/net/NatTraversal.h>
#include <rwe/net/stun_test_util.h>
#include <thread>

namespace rwe
{
    namespace
    {
        using asio::ip::udp;

        template <typename Pred>
        bool waitUntil(Pred pred, std::chrono::milliseconds limit = std::chrono::milliseconds(4000))
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (pred())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return pred();
        }

        udp::endpoint loopback(unsigned short port)
        {
            return udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), port);
        }
    }

    TEST_CASE("NatTraversal learns what both sockets look like from outside")
    {
        constexpr unsigned short discoveryPort = 1381;
        constexpr unsigned short gamePort = 1391;

        SECTION("the discovery socket and the game port are each found, from their own ports")
        {
            FakeStunServer a;
            FakeStunServer b;

            LanDiscovery discovery;
            discovery.start(discoveryPort);
            REQUIRE(discovery.isRunning());

            NatTraversal nat(discovery);
            nat.start(gamePort, std::vector<udp::endpoint>{a.endpoint(), b.endpoint()});

            REQUIRE(waitUntil([&]() { return nat.status().phase == NatTraversal::Phase::Ready; }));
            const auto status = nat.status();
            REQUIRE(status.discoveryAddress);
            REQUIRE(status.gameAddress);
            // The server saw each datagram's real source, which on loopback
            // is the socket's own port: that is the point of asking from the
            // socket and not from a stand-in.
            CHECK(*status.discoveryAddress == loopback(discoveryPort));
            CHECK(*status.gameAddress == loopback(gamePort));
            CHECK(status.problem.empty());

            nat.stop();
        }

        SECTION("an address the router has moved is followed, and the sockets are kept alive")
        {
            // A router drops a mapping that has carried nothing and makes
            // another, at another port, when the socket next sends. A code
            // that was made a minute ago then names nobody. The servers are
            // asked again every keepaliveInterval, which both holds the
            // mapping and notices that it moved.
            std::atomic<int> shift{0};
            FakeStunServer a([&shift](const udp::endpoint& from) {
                return udp::endpoint(from.address(), static_cast<unsigned short>(from.port() + shift));
            });
            FakeStunServer b([&shift](const udp::endpoint& from) {
                return udp::endpoint(from.address(), static_cast<unsigned short>(from.port() + shift));
            });

            LanDiscovery discovery;
            discovery.start(discoveryPort + 7);
            NatTraversal nat(discovery);
            nat.keepaliveInterval = std::chrono::milliseconds(100);
            nat.start(gamePort + 7, std::vector<udp::endpoint>{a.endpoint(), b.endpoint()});

            REQUIRE(waitUntil([&]() { return nat.status().phase == NatTraversal::Phase::Ready; }));
            REQUIRE(nat.status().discoveryAddress);
            CHECK(*nat.status().discoveryAddress == loopback(discoveryPort + 7));

            // Asked again and again, from both sockets, with nobody changing anything.
            const auto before = a.requests();
            REQUIRE(waitUntil([&]() { return a.requests() >= before + 6; }));
            CHECK(nat.status().phase == NatTraversal::Phase::Ready);

            shift = 11;
            REQUIRE(waitUntil([&]() {
                const auto s = nat.status();
                return s.discoveryAddress && s.gameAddress
                    && s.discoveryAddress->port() == discoveryPort + 7 + 11
                    && s.gameAddress->port() == gamePort + 7 + 11;
            }));
            // Both servers moved together, so it is still a router that can be punched.
            CHECK(nat.status().phase == NatTraversal::Phase::Ready);
            nat.stop();
        }

        SECTION("the game port is released when it stops, for the game to bind")
        {
            FakeStunServer a;
            LanDiscovery discovery;
            discovery.start(discoveryPort + 1);
            REQUIRE(discovery.isRunning());

            NatTraversal nat(discovery);
            nat.start(gamePort + 1, std::vector<udp::endpoint>{a.endpoint()});
            REQUIRE(waitUntil([&]() { return nat.status().gameAddress.has_value(); }));
            nat.stop();

            asio::io_context context;
            udp::socket game(context);
            game.open(udp::v4());
            asio::error_code ec;
            game.bind(udp::endpoint(udp::v4(), gamePort + 1), ec);
            CHECK_FALSE(ec);
        }

        SECTION("a port that is already taken is said, not hidden")
        {
            FakeStunServer a;
            LanDiscovery discovery;
            discovery.start(discoveryPort + 2);

            asio::io_context context;
            udp::socket occupant(context);
            occupant.open(udp::v4());
            occupant.bind(udp::endpoint(udp::v4(), gamePort + 2));

            NatTraversal nat(discovery);
            nat.start(gamePort + 2, std::vector<udp::endpoint>{a.endpoint()});
            REQUIRE(waitUntil([&]() { return nat.status().phase == NatTraversal::Phase::Failed; }));
            CHECK(nat.status().problem.find(std::to_string(gamePort + 2)) != std::string::npos);
            nat.stop();
        }

        SECTION("a router that gives every destination its own port is named, and no address is offered as good")
        {
            // Two servers; the second is told a different port for the same
            // socket, which is what a symmetric NAT does.
            FakeStunServer a;
            FakeStunServer b([](const udp::endpoint& from) {
                return udp::endpoint(from.address(), static_cast<unsigned short>(from.port() + 1000));
            });

            LanDiscovery discovery;
            discovery.start(discoveryPort + 3);
            NatTraversal nat(discovery);
            nat.start(gamePort + 3, std::vector<udp::endpoint>{a.endpoint(), b.endpoint()});

            REQUIRE(waitUntil([&]() { return nat.status().phase == NatTraversal::Phase::Failed; }));
            CHECK(nat.status().problem.find("own port") != std::string::npos);
            nat.stop();
        }

        SECTION("no answer from any server is a failure with a reason")
        {
            // Nothing is listening where it points.
            asio::io_context context;
            udp::socket silent(context, udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), 0));

            LanDiscovery discovery;
            discovery.start(discoveryPort + 4);
            NatTraversal nat(discovery);
            nat.start(gamePort + 4, std::vector<udp::endpoint>{silent.local_endpoint()});

            REQUIRE(waitUntil([&]() { return nat.status().phase == NatTraversal::Phase::Failed; },
                std::chrono::milliseconds(8000)));
            CHECK_FALSE(nat.status().problem.empty());
            nat.stop();
        }

        SECTION("discovery traffic on the same socket is not disturbed")
        {
            // The STUN answers share the socket with the lobby. An announcement
            // sent while the probe is running still arrives as one.
            FakeStunServer a;
            LanDiscovery host;
            host.start(discoveryPort + 5);
            LanDiscovery guest;
            guest.start(discoveryPort + 6);

            NatTraversal nat(host);
            nat.start(gamePort + 5, std::vector<udp::endpoint>{a.endpoint()});

            DiscoveredGame game;
            game.session = "natsession";
            game.map = "Coast To Coast";
            game.slots.push_back(DiscoveredSlot{0, "human", "Host", "ARM", 0, -1, ""});
            host.advertise(game);

            REQUIRE(guest.probe("127.0.0.1:" + std::to_string(discoveryPort + 5)));
            REQUIRE(waitUntil([&]() { return !guest.games().empty(); }));
            REQUIRE(waitUntil([&]() { return nat.status().discoveryAddress.has_value(); }));
            CHECK(guest.games()[0].session == "natsession");
            nat.stop();
        }
    }

    TEST_CASE("NatTraversal punches: peers that send to each other's game port hear each other")
    {
        constexpr unsigned short discoveryPortA = 1385;
        constexpr unsigned short discoveryPortB = 1386;
        constexpr unsigned short gamePortA = 1395;
        constexpr unsigned short gamePortB = 1396;

        FakeStunServer stunServer;
        LanDiscovery discoveryA;
        discoveryA.start(discoveryPortA);
        LanDiscovery discoveryB;
        discoveryB.start(discoveryPortB);

        NatTraversal a(discoveryA);
        NatTraversal b(discoveryB);
        a.start(gamePortA, std::vector<udp::endpoint>{stunServer.endpoint()});
        b.start(gamePortB, std::vector<udp::endpoint>{stunServer.endpoint()});
        REQUIRE(waitUntil([&]() { return a.status().gameAddress && b.status().gameAddress; }));

        a.setPunchTargets({*b.status().gameAddress});
        b.setPunchTargets({*a.status().gameAddress});

        REQUIRE(waitUntil([&]() { return !a.heardFrom().empty() && !b.heardFrom().empty(); }));
        CHECK(a.heardFrom()[0] == loopback(gamePortB));
        CHECK(b.heardFrom()[0] == loopback(gamePortA));

        a.stop();
        b.stop();
    }
}
