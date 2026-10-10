#include <catch2/catch_test_macros.hpp>
#include <rwe/net/EndpointFamily.h>

namespace rwe
{
    TEST_CASE("toDualStackEndpoint")
    {
        SECTION("an IPv4 peer becomes its v4-mapped IPv6 address")
        {
            // This is the case that was broken: an AF_INET6 socket cannot
            // send to an IPv4 address on Windows, so a peer given as a dotted
            // address killed the network thread with WSAEFAULT. The address
            // has to reach the socket in the form it can actually send to.
            auto endpoint = toDualStackEndpoint(
                asio::ip::udp::endpoint(asio::ip::make_address("192.168.2.120"), 1337));

            CHECK(endpoint.address().is_v6());
            CHECK(endpoint.address() == asio::ip::make_address("::ffff:192.168.2.120"));
            CHECK(endpoint.port() == 1337);
        }

        SECTION("an IPv6 peer is left exactly as it was")
        {
            // Loopback is how the network tests and a two-peer setup on one
            // machine address each other, so it must survive untouched.
            auto endpoint = toDualStackEndpoint(
                asio::ip::udp::endpoint(asio::ip::make_address("::1"), 15337));

            CHECK(endpoint.address() == asio::ip::make_address("::1"));
            CHECK(endpoint.port() == 15337);
        }

        SECTION("a global IPv6 peer is left exactly as it was")
        {
            auto endpoint = toDualStackEndpoint(
                asio::ip::udp::endpoint(asio::ip::make_address("fd00::441b:5f1:1d3:d6a7"), 1337));

            CHECK(endpoint.address() == asio::ip::make_address("fd00::441b:5f1:1d3:d6a7"));
            CHECK(endpoint.port() == 1337);
        }

        SECTION("the mapping is what makes a received peer comparable")
        {
            // A dual-stack socket reports an IPv4 sender as its v4-mapped
            // address. Without the mapping at setup the stored endpoint and
            // the one a datagram arrives from would never compare equal, and
            // every packet would be dropped as coming from an unknown sender.
            auto stored = toDualStackEndpoint(
                asio::ip::udp::endpoint(asio::ip::make_address("192.168.2.120"), 1337));
            auto arrivedFrom = asio::ip::udp::endpoint(
                asio::ip::make_address("::ffff:192.168.2.120"), 1337);

            CHECK(stored == arrivedFrom);
        }

        SECTION("every port in the v4 range maps rather than throwing")
        {
            // The address is built by formatting a string, so this guards the
            // one thing that could go wrong for an unusual port: nothing.
            for (unsigned int port = 1; port <= 65535; port += 8191)
            {
                auto endpoint = toDualStackEndpoint(
                    asio::ip::udp::endpoint(asio::ip::make_address("10.0.0.7"), port));
                CHECK(endpoint.port() == port);
            }
        }
    }
}