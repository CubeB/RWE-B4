#pragma once

#include <asio.hpp>
#include <optional>
#include <string>

namespace rwe
{
    /**
     * An address a person can read out or paste into a chat window.
     *
     * Two machines behind routers cannot reach each other until each has
     * sent the other a datagram, and each can only do that once it knows the
     * other's address as the internet sees it -- which is what STUN finds
     * out and which changes every session. With no server of ours to carry
     * the two addresses between them, the people are the server: a host's
     * code and a guest's reply are passed through whatever they already
     * talk on. So the code has to survive being typed, read aloud, wrapped
     * by a chat client and mistaken for a word.
     *
     * `RWE-XXXX-XXXX-XXXXX`: thirteen characters of Crockford's base32 (no
     * I, L, O or U, so nothing is confused for a one or a zero or an
     * obscenity) carrying a version nibble, an IPv4 address, a port and an
     * eight-bit check. A typo is a code that does not decode, which is what
     * is wanted; a wrong address that decoded would be a silent timeout.
     * Case, spaces, dashes and the look-alikes O, I and L are accepted when
     * reading, and are one of the reasons the alphabet is this one.
     *
     * It is not secret and not signed: it is somebody's public address, which
     * every server they have ever spoken to has seen.
     */
    class InviteCode
    {
    public:
        /** The written form of an address. Nothing for one that is not an IPv4 endpoint that can be sent to. */
        static std::optional<std::string> encode(const asio::ip::udp::endpoint& endpoint);

        /** The address in a code, or nothing for text that is not a whole, valid one. */
        static std::optional<asio::ip::udp::endpoint> decode(const std::string& text);

        /** Whether decode() would accept the text. */
        static bool isInviteCode(const std::string& text);

        /** `a.b.c.d:port`, which is how the rest of the lobby writes an address. */
        static std::string toAddressString(const asio::ip::udp::endpoint& endpoint);
    };
}
