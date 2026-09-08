#include "tad_util.h"

#include <algorithm>
#include <rwe/io/tad/tad_headers.h>

namespace rwe
{
    namespace
    {
        uint16_t readU16(const uint8_t* p)
        {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }

        void writeU32(uint8_t* p, uint32_t v)
        {
            p[0] = static_cast<uint8_t>(v);
            p[1] = static_cast<uint8_t>(v >> 8);
            p[2] = static_cast<uint8_t>(v >> 16);
            p[3] = static_cast<uint8_t>(v >> 24);
        }

        /** The compressor only ever emits a three-byte header; see tadCompress. */
        const unsigned int CompressHeaderSize = 3;
    }

    TadException::TadException(const std::string& message) : std::runtime_error(message)
    {
    }

    TadChecksum tadDecrypt(TadBytes& data, std::size_t ofs)
    {
        TadChecksum check{0, 0};

        if (data.size() < ofs + 4)
        {
            // Too short to hold a header. The reference pads rather than
            // failing, and demos in the wild contain such packets.
            data.push_back(0x06);
            return check;
        }

        check.stored = readU16(&data[ofs + 1]);

        uint8_t xorKey = 3;
        for (std::size_t i = ofs + 3; i <= data.size() - 4; ++i)
        {
            check.computed += data[i];
            data[i] ^= xorKey;
            ++xorKey;
        }

        return check;
    }

    void tadEncrypt(TadBytes& data, std::size_t ofs)
    {
        if (data.size() < ofs + 4)
        {
            data.push_back(0x06);
            return;
        }

        uint16_t check = 0;
        uint8_t xorKey = 3;
        for (std::size_t i = ofs + 3; i <= data.size() - 4; ++i)
        {
            data[i] ^= xorKey;
            check += data[i];
            ++xorKey;
        }

        data[ofs + 1] = static_cast<uint8_t>(check);
        data[ofs + 2] = static_cast<uint8_t>(check >> 8);
    }

    TadBytes tadDecompress(const uint8_t* data, std::size_t len, unsigned int headerSize, bool& ok)
    {
        ok = true;

        TadBytes result;

        if (len == 0 || data[0] != TadPacketCompressed)
        {
            result.assign(data, data + len);
            return result;
        }

        if (len < headerSize || headerSize < 1)
        {
            ok = false;
            result.assign(data, data + len);
            return result;
        }

        result.reserve(std::max<std::size_t>(0x1000, 2 * len));
        result.assign(data, data + headerSize);
        result[0] = TadPacketUncompressed;

        std::size_t index = headerSize;
        while (index < len)
        {
            unsigned int controlByte = data[index];
            ++index;

            for (unsigned int slot = 0; slot < 8; ++slot)
            {
                if (index >= len)
                {
                    // Ran out of bytes mid-block. The reference signals this by
                    // writing the compressed marker back into the output.
                    ok = false;
                    return result;
                }

                if (((controlByte >> slot) & 1) == 0)
                {
                    result.push_back(data[index]);
                    ++index;
                    continue;
                }

                if (index + 2 > len)
                {
                    // A back-reference needs two bytes and only one is left.
                    // The reference reads past the end here; we stop instead.
                    ok = false;
                    return result;
                }

                unsigned int op = readU16(&data[index]);
                index += 2;

                unsigned int a = op >> 4;
                if (a == 0)
                {
                    return result;
                }

                unsigned int count = (op & 0x0f) + 2;
                a += headerSize;

                // Absolute position into the output, and deliberately
                // self-referential: result grows as it is read, so a source that
                // catches up with the cursor repeats.
                for (unsigned int b = a; b < a + count; ++b)
                {
                    uint8_t byte = b <= result.size() ? result[b - 1] : 0;
                    result.push_back(byte);
                }
            }
        }

        return result;
    }

    TadBytes tadDecompress(const TadBytes& data, unsigned int headerSize, bool& ok)
    {
        return tadDecompress(data.data(), data.size(), headerSize, ok);
    }

    TadBytes tadCompress(const TadBytes& data)
    {
        // Transcribed from the reference, which works in one-based indices over
        // the payload and assumes a three-byte header. Left in that shape
        // deliberately: it is the inverse of a decoder whose offsets are
        // one-based, and renumbering it is how the round trip gets broken.
        TadBytes result;

        unsigned int count = 7;
        unsigned int controlIndex = 0;
        std::size_t index = 4;
        unsigned int match = 0;

        while (index < data.size() + 1)
        {
            if (count == 7)
            {
                count = 0;
                result.push_back(0);
                controlIndex = static_cast<unsigned int>(result.size());
            }
            else
            {
                ++count;
            }

            if (index < 6 || index > 2000)
            {
                result.push_back(data[index - 1]);
                ++index;
                continue;
            }

            unsigned int matchLength = 2;

            for (std::size_t a = 4; a < index - 1; ++a)
            {
                unsigned int candidate = 0;
                while (a + candidate < index
                    && index + candidate < data.size()
                    && data[a + candidate - 1] == data[index + candidate - 1])
                {
                    ++candidate;
                }

                if (candidate > matchLength)
                {
                    matchLength = candidate;
                    match = static_cast<unsigned int>(a);
                    if (matchLength > 17)
                    {
                        break;
                    }
                }
            }

            // A run of the immediately preceding byte, which the search above
            // cannot express because it stops at the cursor.
            unsigned int runLength = 0;
            while (index + runLength < data.size() && data[index + runLength - 1] == data[index - 2])
            {
                ++runLength;
            }
            if (runLength > matchLength)
            {
                matchLength = runLength;
                match = static_cast<unsigned int>(index - 1);
            }

            if (matchLength > 2)
            {
                result[controlIndex - 1] |= static_cast<uint8_t>(1u << count);
                matchLength = (matchLength - 2) & 0x0f;
                unsigned int op = ((match - CompressHeaderSize) << 4) | matchLength;
                result.push_back(static_cast<uint8_t>(op));
                result.push_back(static_cast<uint8_t>(op >> 8));
                index += matchLength + 2;
            }
            else
            {
                result.push_back(data[index - 1]);
                ++index;
            }
        }

        if (count == 7)
        {
            result.push_back(0xff);
        }
        else
        {
            result[controlIndex - 1] |= static_cast<uint8_t>(0xffu << (count + 1));
        }

        // A zero offset in the next slot terminates the stream.
        result.push_back(0);
        result.push_back(0);

        if (result.size() + CompressHeaderSize < data.size())
        {
            TadBytes out;
            out.reserve(result.size() + CompressHeaderSize);
            out.push_back(TadPacketCompressed);
            out.push_back(data[1]);
            out.push_back(data[2]);
            out.insert(out.end(), result.begin(), result.end());
            return out;
        }

        // Not worth compressing; emit the input as a plain packet.
        TadBytes out = data;
        out[0] = TadPacketUncompressed;
        return out;
    }

    unsigned int tadExpectedSubPacketSize(const uint8_t* s, std::size_t sz)
    {
        if (sz == 0)
        {
            return 0;
        }

        switch (static_cast<TadSubPacketCode>(s[0]))
        {
            case TadSubPacketCode::Zero:
            {
                // A run of zero bytes, however long it happens to be.
                unsigned int len = 0;
                while (len < sz && s[len] == 0)
                {
                    ++len;
                }
                return len;
            }

            case TadSubPacketCode::Ping:
                return 13;
            case TadSubPacketCode::Unk03:
                return 7;

            case TadSubPacketCode::Chat:
            {
                if (sz < 65)
                {
                    return 65;
                }
                if (s[64] != 0)
                {
                    // Older recorders sometimes emit more text than they should,
                    // but always as a single packet. If map position reporting is
                    // on, the last five bytes are a 0xfc that belongs to the next
                    // subpacket.
                    unsigned int len = static_cast<unsigned int>(sz);
                    if (len >= 5 && static_cast<TadSubPacketCode>(s[len - 5]) == TadSubPacketCode::MapPosition)
                    {
                        len -= 5;
                    }
                    return len;
                }
                return 65;
            }

            case TadSubPacketCode::PadEncrypt:
                return 1;
            case TadSubPacketCode::Unk07:
                return 1;
            case TadSubPacketCode::LoadingStarted:
                return 1;
            case TadSubPacketCode::UnitBuildStarted:
                return 23;
            case TadSubPacketCode::Unk0a:
                return 7;
            case TadSubPacketCode::UnitTakeDamage:
                return 9;
            case TadSubPacketCode::UnitKilled:
                return 11;
            case TadSubPacketCode::WeaponFired:
                return 36;
            case TadSubPacketCode::AreaOfEffect:
                return 14;
            case TadSubPacketCode::FeatureAction:
                return 6;
            case TadSubPacketCode::UnitStartScript:
                return 22;
            case TadSubPacketCode::UnitState:
                return 4;
            case TadSubPacketCode::UnitBuildFinished:
                return 5;

                // PlaySound (0x13) is deliberately absent: the reference names the
                // code but has no size for it, so it falls through to 0 and gets
                // reported as unknown rather than guessed at. See docs/TA-DEMOS.md.

            case TadSubPacketCode::GiveUnit:
                return 24;
            case TadSubPacketCode::Start15:
                return 1;
            case TadSubPacketCode::ShareResources:
                return 17;
            case TadSubPacketCode::Unk17:
                return 2;
            case TadSubPacketCode::HostMigration:
                return 2;
            case TadSubPacketCode::Speed:
                return 3;
            case TadSubPacketCode::UnitData:
                return 14;
            case TadSubPacketCode::Reject:
                return 6;
            case TadSubPacketCode::Start1e:
                return 2;
            case TadSubPacketCode::Unk1f:
                return 5;
            case TadSubPacketCode::PlayerInfo:
                // 186, not the 192 the reference's table says. The reference
                // knows about "mysteriously short-sized PlayerInfo packets" and
                // decodes them at exactly this length, but never propagated that
                // to the size it walks with, so its walker over-runs by six into
                // the alliance declarations that follow. 192 is the length of the
                // lobby status packet in the demo header, which is where the
                // reference got it. Measured over thirteen games: 1123
                // occurrences, all 186, and assuming 192 loses 5134 of the 5638
                // 0x23 records and 1051 of the 1123 0x24 records.
                return 186;
            case TadSubPacketCode::Unk21:
                return 10;
            case TadSubPacketCode::Ident3:
                return 6;
            case TadSubPacketCode::Ally:
                return 14;
            case TadSubPacketCode::Team:
                return 6;
            case TadSubPacketCode::Ident2:
                return 41;
            case TadSubPacketCode::PlayerResourceInfo:
                return 58;
            case TadSubPacketCode::Unk29:
                return 3;
            case TadSubPacketCode::LoadingProgress:
                return 2;

            case TadSubPacketCode::UnitStatAndMove:
                return sz >= 3 ? readU16(&s[1]) : 0;

            case TadSubPacketCode::Unk2e:
                return 9;

            case TadSubPacketCode::ThaldrenExtended:
                return sz >= 3 ? readU16(&s[1]) + 3u : 0;

            case TadSubPacketCode::Unkf6:
                return 1;
            case TadSubPacketCode::AllyChat:
                return 73;
            case TadSubPacketCode::ReplayerServer:
                return 1;

            case TadSubPacketCode::RecorderDataConnect:
                return sz >= 2 ? s[1] + 3u : 0;

            case TadSubPacketCode::MapPosition:
                return 5;

            case TadSubPacketCode::SmartPakTickOther:
            {
                if (sz < 3)
                {
                    return 0;
                }
                unsigned int declared = readU16(&s[1]);
                return declared >= 4 ? declared - 4 : 0;
            }

            case TadSubPacketCode::SmartPakTickStart:
                return 5;
            case TadSubPacketCode::SmartPakTick:
                return 1;

            default:
                return 0;
        }
    }

    std::vector<TadBytes> tadSplitSubPackets(const uint8_t* data, std::size_t len, TadWalkStats& stats)
    {
        std::vector<TadBytes> result;

        const uint8_t* ptr = data;
        const uint8_t* end = data + len;

        while (ptr < end)
        {
            auto remaining = static_cast<std::size_t>(end - ptr);
            unsigned int size = tadExpectedSubPacketSize(ptr, remaining);

            if (size == 0)
            {
                ++stats.unknownCodes;
                size = static_cast<unsigned int>(remaining);
            }
            else if (size > remaining)
            {
                ++stats.truncated;
                size = static_cast<unsigned int>(remaining);
            }

            result.emplace_back(ptr, ptr + size);
            ptr += size;
        }

        return result;
    }

    std::vector<TadBytes> tadUnsmartpak(
        const TadBytes& data,
        bool hasTimestamp,
        bool hasChecksum,
        TadWalkStats& stats)
    {
        if (data.empty())
        {
            return {};
        }

        TadBytes buffer;
        const uint8_t* ptr = data.data();
        const uint8_t* end = ptr + data.size();

        if (data[0] == TadPacketCompressed)
        {
            bool ok = true;
            buffer = tadDecompress(data, hasChecksum ? 3 : 1, ok);
            if (!ok)
            {
                ++stats.failedDecompressions;
            }
            ptr = buffer.data();
            end = ptr + buffer.size();
        }

        // Skip the type byte, then whatever else the payload carries in front of
        // its subpackets. A demo packet record carries neither a checksum nor a
        // timestamp, except on version 3, which has the timestamp.
        std::size_t headerSize = 1;
        if (hasChecksum)
        {
            headerSize += 2;
        }
        if (hasTimestamp)
        {
            headerSize += 4;
        }

        if (static_cast<std::size_t>(end - ptr) < headerSize)
        {
            ++stats.truncated;
            return {};
        }
        ptr += headerSize;

        auto raw = tadSplitSubPackets(ptr, static_cast<std::size_t>(end - ptr), stats);

        std::vector<TadBytes> result;
        result.reserve(raw.size());

        uint32_t packetSerial = 0;

        for (auto& subPacket : raw)
        {
            if (subPacket.empty())
            {
                continue;
            }

            switch (static_cast<TadSubPacketCode>(subPacket[0]))
            {
                case TadSubPacketCode::SmartPakTickStart:
                {
                    if (subPacket.size() >= 5)
                    {
                        packetSerial = static_cast<uint32_t>(subPacket[1])
                            | (static_cast<uint32_t>(subPacket[2]) << 8)
                            | (static_cast<uint32_t>(subPacket[3]) << 16)
                            | (static_cast<uint32_t>(subPacket[4]) << 24);
                    }
                    break;
                }

                case TadSubPacketCode::SmartPakTick:
                {
                    // Expands to the minimal unit-state packet, whose declared
                    // length of 11 is the 0x000b in bytes 1 and 2.
                    TadBytes expanded{0x2c, 0x0b, 0x00, 0, 0, 0, 0, 0xff, 0xff, 0x01, 0x00};
                    writeU32(&expanded[3], packetSerial);
                    ++packetSerial;
                    result.push_back(std::move(expanded));
                    break;
                }

                case TadSubPacketCode::SmartPakTickOther:
                {
                    if (subPacket.size() < 3)
                    {
                        ++stats.truncated;
                        break;
                    }

                    // Splice the serial back in after the length field. Bytes 1
                    // and 2 already hold the length the packet will have once the
                    // four serial bytes are restored.
                    TadBytes expanded;
                    expanded.reserve(subPacket.size() + 4);
                    expanded.insert(expanded.end(), subPacket.begin(), subPacket.begin() + 3);
                    expanded.insert(expanded.end(), 4, 0);
                    expanded.insert(expanded.end(), subPacket.begin() + 3, subPacket.end());
                    writeU32(&expanded[3], packetSerial);
                    ++packetSerial;
                    expanded[0] = 0x2c;
                    result.push_back(std::move(expanded));
                    break;
                }

                default:
                    result.push_back(std::move(subPacket));
                    break;
            }
        }

        return result;
    }
}
