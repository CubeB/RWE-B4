#include "TadReader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace rwe
{
    namespace
    {
        /** The obfuscation applied to the player address sector. */
        const uint8_t PlayerAddrXorKey = 42;

        /** Offset of the DirectPlay id within a decoded player status message. */
        const std::size_t DplayIdOffset = 0x91;

        uint16_t readU16(const uint8_t* p)
        {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }

        uint32_t readU32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0])
                | (static_cast<uint32_t>(p[1]) << 8)
                | (static_cast<uint32_t>(p[2]) << 16)
                | (static_cast<uint32_t>(p[3]) << 24);
        }

        /**
         * A string that occupies the rest of a record, stopping early at a NUL if
         * there is one. The recorder terminates these only when there is room --
         * the header's map name runs to the last byte of its record.
         */
        std::string readTrailingString(const TadBytes& record, std::size_t offset)
        {
            if (offset >= record.size())
            {
                return std::string();
            }

            auto begin = record.begin() + offset;
            auto end = std::find(begin, record.end(), 0);
            return std::string(begin, end);
        }

        [[noreturn]] void fail(const std::string& message, std::streampos position)
        {
            std::ostringstream ss;
            ss << message << " at position " << position;
            throw TadException(ss.str());
        }

        /**
         * Reads one length-prefixed record. Returns false at a clean end of file,
         * throws if the file ends part-way through one.
         */
        bool readRecord(std::istream& stream, TadBytes& out)
        {
            auto position = stream.tellg();

            uint8_t lengthBytes[2];
            stream.read(reinterpret_cast<char*>(lengthBytes), 2);
            auto lengthBytesRead = stream.gcount();

            if (lengthBytesRead == 0)
            {
                return false;
            }
            if (lengthBytesRead < 2)
            {
                fail("Truncated record length", position);
            }

            unsigned int length = readU16(lengthBytes);
            if (length > TadMaxRecordLength || length < TadMinRecordLength)
            {
                std::ostringstream ss;
                ss << "Unrealistic record length " << length;
                fail(ss.str(), position);
            }

            // The length counts its own two bytes.
            out.resize(length - 2);
            stream.read(reinterpret_cast<char*>(out.data()), out.size());
            if (static_cast<std::size_t>(stream.gcount()) < out.size())
            {
                fail("Truncated record body", position);
            }

            return true;
        }

        TadBytes readRecordOrFail(std::istream& stream, const char* what)
        {
            TadBytes record;
            if (!readRecord(stream, record))
            {
                throw TadException(std::string("File ended before the ") + what + " record");
            }
            return record;
        }

        TadHeader parseHeader(const TadBytes& record)
        {
            if (record.size() < sizeof(TadHeaderPrefix))
            {
                throw TadException("Header record is too short");
            }

            TadHeaderPrefix prefix;
            std::memcpy(&prefix, record.data(), sizeof(prefix));

            if (std::memcmp(prefix.magic, TadMagicNumber, sizeof(TadMagicNumber)) != 0)
            {
                throw TadException("Not a TA demo: bad magic");
            }

            TadHeader header;
            header.version = prefix.version;
            header.numPlayers = prefix.numPlayers;
            header.maxUnits = prefix.maxUnits;

            if (header.version < TadMinVersion || header.version > TadMaxVersion)
            {
                throw TadException("Unsupported demo version: " + std::to_string(header.version));
            }
            if (header.numPlayers > 10)
            {
                throw TadException("Unrealistic number of players: " + std::to_string(header.numPlayers));
            }
            if (header.maxUnits > 5000)
            {
                throw TadException("Unrealistic max units: " + std::to_string(header.maxUnits));
            }

            header.mapName = readTrailingString(record, sizeof(TadHeaderPrefix));
            if (header.mapName.size() > 64)
            {
                throw TadException("Unrealistic map name length: " + std::to_string(header.mapName.size()));
            }
            for (auto ch : header.mapName)
            {
                if (!std::isprint(static_cast<unsigned char>(ch)))
                {
                    throw TadException("Map name contains unprintable characters: " + header.mapName);
                }
            }

            return header;
        }
    }

    void readTad(std::istream& stream, TadHandler& handler)
    {
        auto header = parseHeader(readRecordOrFail(stream, "header"));
        handler.onHeader(header);

        if (header.version > 4)
        {
            auto extraHeader = readRecordOrFail(stream, "extra header");
            if (extraHeader.size() < sizeof(uint32_t))
            {
                throw TadException("Extra header record is too short");
            }

            uint32_t numSectors = readU32(extraHeader.data());
            if (numSectors > 10000)
            {
                throw TadException("Unrealistic number of extra sectors: " + std::to_string(numSectors));
            }

            for (unsigned int i = 0; i < numSectors; ++i)
            {
                auto record = readRecordOrFail(stream, "extra sector");
                if (record.size() < sizeof(uint32_t))
                {
                    throw TadException("Extra sector record is too short");
                }

                TadExtraSector sector;
                sector.type = readU32(record.data());
                sector.data.assign(record.begin() + sizeof(uint32_t), record.end());

                if (static_cast<TadExtraSectorType>(sector.type) == TadExtraSectorType::PlayerAddr)
                {
                    for (auto& b : sector.data)
                    {
                        b ^= PlayerAddrXorKey;
                    }
                }

                handler.onExtraSector(sector, i, numSectors);
            }
        }

        for (unsigned int i = 0; i < header.numPlayers; ++i)
        {
            auto record = readRecordOrFail(stream, "player");
            if (record.size() < sizeof(TadPlayerPrefix))
            {
                throw TadException("Player record is too short");
            }

            TadPlayer player;
            player.color = record[0];
            player.side = static_cast<int8_t>(record[1]);
            player.number = record[2];
            player.name = readTrailingString(record, sizeof(TadPlayerPrefix));

            handler.onPlayer(player, i, header.numPlayers);
        }

        for (unsigned int i = 0; i < header.numPlayers; ++i)
        {
            auto record = readRecordOrFail(stream, "player status");
            if (record.empty())
            {
                throw TadException("Player status record is empty");
            }

            TadPlayerStatus status;
            status.number = record[0];

            // The player number occupies byte 0, so the packet proper -- and
            // with it the XOR key counter -- starts at byte 1.
            TadBytes payload = record;
            status.checksum = tadDecrypt(payload, 1);

            bool ok = true;
            auto decompressed = tadDecompress(payload.data() + 1, payload.size() - 1, 3, ok);

            // The first seven bytes are the DirectPlay framing.
            if (decompressed.size() > 7)
            {
                status.statusMessage.assign(decompressed.begin() + 7, decompressed.end());
            }

            if (status.statusMessage.size() >= DplayIdOffset + sizeof(uint32_t))
            {
                status.dplayId = readU32(status.statusMessage.data() + DplayIdOffset);
            }

            handler.onPlayerStatus(status, i, header.numPlayers);
        }

        handler.onUnitData(readRecordOrFail(stream, "unit data"));

        TadBytes record;
        while (readRecord(stream, record))
        {
            if (record.size() < sizeof(TadPacketPrefix))
            {
                throw TadException("Packet record is too short");
            }

            TadPacket packet;
            packet.time = readU16(record.data());
            packet.sender = record[2];

            TadBytes payload(record.begin() + sizeof(TadPacketPrefix), record.end());

            TadWalkStats stats;
            // Only version 3 puts a timestamp in front of the subpackets, and a
            // demo's packet records never carry the wire checksum.
            auto subPackets = tadUnsmartpak(payload, header.version == 3, false, stats);

            handler.onPacket(packet, subPackets, stats);
        }
    }
}
