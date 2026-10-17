#pragma once

// The envelope every TA packet carries, live or replayed: decrypt, decompress,
// skip seven bytes -- type, checksum, a u32 -- and walk the subpackets.
// docs/TA-NETWORK.md, "TA packets". The transforms are rwe/io/tad/tad_util.h's,
// which the demo reader shares, so this is the envelope and nothing else.

#include <rwe/io/tad/tad_headers.h>
#include <rwe/io/tad/tad_util.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rwe
{
    /** Type, checksum u16, the marker u32. */
    inline constexpr std::size_t TaPacketHeaderSize = 7;

    /** Why a host turns a player away: the byte after the id in a 0x1b. */
    inline constexpr std::uint8_t TaRejectClosed = 3;

    /** The password a joiner sent is not the host's (BT-242). */
    inline constexpr std::uint8_t TaRejectPassword = 4;

    /** The joiner's player data is not the 21 bytes with the protocol tag TA writes (BT-242). */
    inline constexpr std::uint8_t TaRejectVersion = 8;

    /** A joiner that has been silent for too long (BT-233). */
    inline constexpr std::uint8_t TaRejectSilent = 6;

    /** The size of a 0x05 chat subpacket: the code and a 64-byte body (BT-203). */
    inline constexpr std::size_t TaChatSubPacketSize = 65;

    /** What a reply wears. A sender's own traffic wears a count instead. */
    inline constexpr std::uint32_t TaReplyMarker = 0xFFFFFFFF;

    /**
     * The u32 a sender puts on its own traffic. The captures show a per-sender
     * count that falls by one for every packet that carries one, and both peers
     * in every recording start one below 0xffffffff, so a sender's own traffic
     * never wears the value a reply wears. Nothing seen depends on the value.
     */
    class TaMarkerCounter
    {
    public:
        /** The count for the next packet, one lower than the last. */
        std::uint32_t take() { return --next_; }

    private:
        std::uint32_t next_{0xFFFFFFFF};
    };

    /** A packet's contents: its framing, its marker, and its subpackets. */
    struct TaPacket
    {
        /** TadPacketUncompressed or TadPacketCompressed, as it arrived. */
        std::uint8_t type{TadPacketUncompressed};

        /** The u32 at offset 3. */
        std::uint32_t marker{TaReplyMarker};

        std::vector<TadBytes> subpackets;

        /**
         * The bytes this packet goes on the wire as: the header, the subpackets,
         * compressed when type says so, then encrypted.
         */
        TadBytes build() const;
    };

    /** What a parse found, next to the packet it made. */
    struct TaPacketParse
    {
        TaPacket packet;

        /** False if the stored checksum did not match the bytes; the walk carries on regardless. */
        bool checksumValid{false};

        /** Subpackets the walk could not size, or that ran past the end of the payload. */
        TadWalkStats stats;
    };

    /**
     * Parses application bytes into a TA packet.
     *
     * Nothing is returned for bytes too short to hold a header, and nothing for a
     * compressed packet whose stream ran out mid-slot: both cost that packet and
     * nothing else. A type byte that is neither 0x03 nor 0x04 is read as plain,
     * as the reference does, which keeps a packet RWE has never seen from being
     * dropped at the door.
     */
    std::optional<TaPacketParse> taParsePacket(std::span<const std::uint8_t> bytes);

    /**
     * 0x1b: a player is turned away, six bytes -- the code, the rejected
     * player's id and the reason (BT-242). A joiner reads its own id in it and
     * leaves the session.
     */
    TadBytes taBuildReject(std::uint32_t playerId, std::uint8_t reason);

    /**
     * 0x05: one line of chat to a TA peer, 65 bytes -- the code and a 64-byte
     * body (BT-203). The line is the whole text the peer prints, the sender's
     * name already inside it as `<Name> text` or `<Name->Allies> text`, so the
     * receiver adds nothing. The original copies the line with a bounded
     * 64-byte copy and its receiver keeps 63 characters, so the line is cut to
     * 63 bytes (never in the middle of a UTF-8 sequence) and padded with NULs.
     */
    TadBytes taBuildChat(const std::string& line);
}
