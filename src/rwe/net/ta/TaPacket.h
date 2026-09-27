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
#include <vector>

namespace rwe
{
    /** Type, checksum u16, the marker u32. */
    inline constexpr std::size_t TaPacketHeaderSize = 7;

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
}
