#pragma once

// The three transforms that stand between a demo record and its subpackets,
// plus the subpacket length table.
//
// Ported from ta-forever/gpgnet4ta, libs/tapacket/TPacket.cpp (MIT licence).
// See docs/TA-DEMOS.md.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace rwe
{
    class TadException : public std::runtime_error
    {
    public:
        explicit TadException(const std::string& message);
    };

    using TadBytes = std::vector<uint8_t>;

    struct TadChecksum
    {
        /** The checksum stored in the packet. */
        uint16_t stored;

        /** The checksum computed while walking it. */
        uint16_t computed;

        bool matches() const { return stored == computed; }
    };

    /**
     * Undoes the XOR obfuscation applied to a TA network packet, in place.
     *
     * data[ofs] is a type byte and data[ofs+1..ofs+2] the stored checksum; the
     * body from ofs+3 to size-4 inclusive is XORed with a counter that starts at
     * 3 and advances with the cursor. The counter equals the absolute index only
     * when ofs is 0 -- the demo's player status message uses ofs 1, where it
     * does not.
     *
     * The checksum sums the *ciphertext*, so it is accumulated before each byte
     * is XORed here and after each byte is XORed in tadEncrypt.
     *
     * A packet too short to hold a header gets a single 0x06 byte appended and
     * is otherwise left alone, which is what the reference does.
     */
    TadChecksum tadDecrypt(TadBytes& data, std::size_t ofs);

    /**
     * The inverse of tadDecrypt, writing the checksum back in.
     *
     * The reference only ever encrypts at offset 0 and uses the absolute index
     * as its key; the counter form here agrees with it there and generalises to
     * the offset the demo's player status message uses.
     */
    void tadEncrypt(TadBytes& data, std::size_t ofs = 0);

    /**
     * Expands a compressed packet payload. Returns the input unchanged unless
     * its first byte is 0x04.
     *
     * headerSize bytes are copied through untouched (with the type byte
     * rewritten to 0x03, so the output is a well-formed plain packet) and the
     * compressed stream begins after them: a control byte, then eight slots,
     * where a clear bit is a literal and a set bit is a little-endian uint16
     * whose top twelve bits are an offset and bottom four a length.
     *
     * The offset is ABSOLUTE into the output buffer, not a distance behind the
     * cursor -- getting that wrong decodes correctly for a while and then goes
     * wrong in the middle. The copy is also self-referential: the output grows
     * as it is read, so a source that runs into the cursor repeats. Reads past
     * the end of the output produce zero, and an offset of zero terminates.
     *
     * @param ok set to false if the stream ran out of bytes mid-slot.
     */
    TadBytes tadDecompress(const uint8_t* data, std::size_t len, unsigned int headerSize, bool& ok);
    TadBytes tadDecompress(const TadBytes& data, unsigned int headerSize, bool& ok);

    /**
     * The inverse of tadDecompress. The engine never needs this -- it exists so
     * that the decompressor can be round-trip tested, which docs/TA-DEMOS.md
     * asks for before anything else.
     */
    TadBytes tadCompress(const TadBytes& data);

    /**
     * The size in bytes of the subpacket starting at s, or 0 if the code is
     * unknown or the buffer is too short to tell.
     */
    unsigned int tadExpectedSubPacketSize(const uint8_t* s, std::size_t sz);

    /** Counts of everything that went wrong while walking a packet. */
    struct TadWalkStats
    {
        /** Subpackets whose first byte is not in the length table. */
        unsigned int unknownCodes = 0;

        /** Subpackets whose declared length runs past the end of the buffer. */
        unsigned int truncated = 0;

        /** Payloads whose compressed stream ran out of bytes. */
        unsigned int failedDecompressions = 0;

        unsigned int total() const { return unknownCodes + truncated + failedDecompressions; }
    };

    /**
     * Splits a decompressed, header-stripped payload into subpackets.
     *
     * A subpacket the table cannot size is emitted whole (the rest of the
     * buffer) and counted in stats, rather than being silently absorbed: a
     * desynchronised walk is the failure this code exists to rule out, so it has
     * to be visible.
     */
    std::vector<TadBytes> tadSplitSubPackets(const uint8_t* data, std::size_t len, TadWalkStats& stats);

    /**
     * Undoes the tick-coalescing the SmartPak mod applies, yielding the
     * subpackets of one packet record. Demos recorded before SmartPak pass
     * through unchanged.
     *
     * Decompresses if needed, skips the type byte (plus two more if the payload
     * carries a checksum and four more if it carries a timestamp -- demo packet
     * records carry neither except on version 3, which has the timestamp), then
     * walks the payload maintaining a running packet serial: 0xfe sets it, 0xff
     * expands to a synthetic 0x2c, and 0xfd becomes a 0x2c with the serial
     * spliced in.
     */
    std::vector<TadBytes> tadUnsmartpak(
        const TadBytes& data,
        bool hasTimestamp,
        bool hasChecksum,
        TadWalkStats& stats);
}
