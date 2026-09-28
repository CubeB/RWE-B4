#include "TaPacket.h"

namespace rwe
{
    namespace
    {
        std::uint32_t readU32(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0])
                | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16)
                | (static_cast<std::uint32_t>(p[3]) << 24);
        }
    }

    TadBytes TaPacket::build() const
    {
        TadBytes plain;
        plain.reserve(TaPacketHeaderSize);
        plain.push_back(TadPacketUncompressed);
        plain.push_back(0);
        plain.push_back(0);
        for (int i = 0; i < 4; ++i)
        {
            plain.push_back(static_cast<std::uint8_t>(marker >> (8 * i)));
        }

        for (const auto& subpacket : subpackets)
        {
            plain.insert(plain.end(), subpacket.begin(), subpacket.end());
        }

        // tadCompress falls back to the plain packet when compression would not
        // pay, so a type of 0x04 that comes back as 0x03 is a shorter packet of
        // the same contents, not a failure.
        TadBytes out = type == TadPacketCompressed ? tadCompress(plain) : plain;
        tadEncrypt(out);
        return out;
    }

    std::optional<TaPacketParse> taParsePacket(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() < TaPacketHeaderSize)
        {
            return std::nullopt;
        }

        TadBytes encrypted(bytes.begin(), bytes.end());
        TadChecksum checksum = tadDecrypt(encrypted, 0);

        bool decompressed = true;
        TadBytes plain = tadDecompress(encrypted, 3, decompressed);
        if (!decompressed || plain.size() < TaPacketHeaderSize)
        {
            return std::nullopt;
        }

        TaPacketParse result;
        result.checksumValid = checksum.matches();
        result.packet.type = encrypted[0];
        result.packet.marker = readU32(&plain[3]);
        result.packet.subpackets = tadSplitSubPackets(
            plain.data() + TaPacketHeaderSize,
            plain.size() - TaPacketHeaderSize,
            result.stats);
        return result;
    }
}
