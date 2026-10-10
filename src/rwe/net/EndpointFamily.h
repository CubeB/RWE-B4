#pragma once

#include <asio.hpp>

namespace rwe
{
    /**
     * The address a peer is really at, for a socket that is really v6.
     *
     * Both network services open their socket as AF_INET6 and keep it that
     * way, because the existing tests and the loopback setup address peers as
     * ::1. But a peer given as a dotted address resolves to an IPv4
     * endpoint, and Winsock refuses send_to() from an AF_INET6 socket to an
     * IPv4 address: the sockaddr handed to the kernel is an IPv4 one on an
     * IPv6 socket, and the call fails with WSAEFAULT rather than delivering.
     * Linux accepts the mismatch and maps it internally, which is why this
     * only ever appeared on Windows.
     *
     * The fix is to put the address in the form the socket can actually send
     * to: an IPv4 address written as its v4-mapped IPv6 address,
     * ::ffff:a.b.c.d. An IPv6 peer is already in that form and comes back
     * unchanged.
     *
     * It also keeps peer recognition working. A dual-stack socket reports an
     * IPv4 sender as its v4-mapped address, so a peer that stored the
     * endpoint from this function compares equal to the endpoint its
     * datagram arrives from -- without the mapping, the stored v4 endpoint
     * never matches the v4-mapped one a received packet reports, and every
     * datagram is discarded as coming from an unknown sender.
     *
     * The socket must be dual-stack for this to receive as well as send;
     * see setDualStack.
     */
    inline asio::ip::udp::endpoint toDualStackEndpoint(const asio::ip::udp::endpoint& endpoint)
    {
        if (!endpoint.address().is_v4())
        {
            return endpoint;
        }

        // This asio only builds an address from a string, which is also the
        // clearest statement of what the result is: an IPv4 address written
        // in the IPv6 form ::ffff:a.b.c.d. Once per peer, at setup.
        return asio::ip::udp::endpoint(
            asio::ip::make_address("::ffff:" + endpoint.address().to_v4().to_string()),
            endpoint.port());
    }

    /**
     * Lets an IPv6 socket carry IPv4 traffic, which toDualStackEndpoint needs:
     * with v6only set, the v4-mapped addresses it produces would be accepted
     * as destinations but never arrive from anyone.
     */
    inline void setDualStack(asio::ip::udp::socket& socket)
    {
        socket.set_option(asio::ip::v6_only(false));
    }
}