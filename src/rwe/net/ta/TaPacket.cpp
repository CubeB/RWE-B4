#include "TaPacket.h"

#include <algorithm>

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

    TadBytes taBuildReject(std::uint32_t playerId, std::uint8_t reason)
    {
        TadBytes out{static_cast<std::uint8_t>(TadSubPacketCode::Reject)};
        for (int i = 0; i < 4; ++i)
        {
            out.push_back(static_cast<std::uint8_t>(playerId >> (8 * i)));
        }
        out.push_back(reason);
        return out;
    }

    TadBytes taBuildChat(const std::string& line)
    {
        constexpr std::size_t MaxLine = TaChatSubPacketSize - 2;

        std::size_t length = std::min(line.size(), MaxLine);
        // Back off a cut that lands inside a multi-byte sequence: a
        // continuation byte is 10xxxxxx, and the sequence it belongs to
        // starts before it.
        if (length < line.size())
        {
            while (length > 0 && (static_cast<std::uint8_t>(line[length]) & 0xC0) == 0x80)
            {
                --length;
            }
        }

        TadBytes out(TaChatSubPacketSize, 0);
        out[0] = static_cast<std::uint8_t>(TadSubPacketCode::Chat);
        for (std::size_t i = 0; i < length; ++i)
        {
            auto c = static_cast<std::uint8_t>(line[i]);
            // A NUL inside the line would end it early on the other side.
            out[1 + i] = c == 0 ? static_cast<std::uint8_t>('?') : c;
        }
        return out;
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
