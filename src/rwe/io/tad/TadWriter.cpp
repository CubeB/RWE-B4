#include "TadWriter.h"

#include <algorithm>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tad/tad_headers.h>
#include <utility>

namespace rwe
{
    namespace
    {
        /** The obfuscation the reader undoes on a player address sector. */
        const uint8_t PlayerAddrXorKey = 42;

        /**
         * The status message is the long form of the 0x20 player info
         * subpacket: 192 bytes with the DirectPlay id at 0x91. Every one of the
         * 89 status messages in the corpus decoded to this length (see
         * docs/TA-DEMOS.md), and it is what makes the record the lobby status
         * packet rather than the 186-byte form the packet stream carries.
         */
        const std::size_t StatusMessageSize = 192;
        const std::size_t StatusDplayIdOffset = 0x91;

        /**
         * On the wire a packet is a type byte, a checksum, a four-byte SmartPak
         * serial and then the subpackets. The reader drops those first seven
         * bytes of the decompressed form, so the serial sits in front of the
         * message; demo 15145's status records carry 0xffffffff there, and
         * this writes zeros, which the reader cannot tell apart.
         */
        const std::size_t StatusMessageOffset = 7;

        const uint8_t StatusMessageCode = static_cast<uint8_t>(TadSubPacketCode::PlayerInfo);

        /**
         * Synthetic ids run upwards from here, below the pseudo-entry's
         * 0x92549357, so the pseudo-entry always sorts where the corpus puts
         * it.
         */
        const uint32_t SyntheticIdBase = 0x10000000u;

        /** The value packed into a restricted entry that is a real unit. */
        const uint32_t UnitValue = 0xffff0101u;

        /** The same with the flag byte cleared, as the pseudo-entry reads. */
        const uint32_t PseudoValue = 0xffff0001u;

        /** 0x1a entries are 14 bytes: code, sub, four zeros, id, value. */
        const std::size_t UnitTableEntrySize = 14;

        void appendU16(TadBytes& bytes, uint16_t value)
        {
            bytes.push_back(static_cast<uint8_t>(value));
            bytes.push_back(static_cast<uint8_t>(value >> 8));
        }

        void appendU32(TadBytes& bytes, uint32_t value)
        {
            bytes.push_back(static_cast<uint8_t>(value));
            bytes.push_back(static_cast<uint8_t>(value >> 8));
            bytes.push_back(static_cast<uint8_t>(value >> 16));
            bytes.push_back(static_cast<uint8_t>(value >> 24));
        }

        void writeU32(uint8_t* p, uint32_t value)
        {
            p[0] = static_cast<uint8_t>(value);
            p[1] = static_cast<uint8_t>(value >> 8);
            p[2] = static_cast<uint8_t>(value >> 16);
            p[3] = static_cast<uint8_t>(value >> 24);
        }

        bool containsNul(const std::string& s)
        {
            return s.find('\0') != std::string::npos;
        }

        TadBytes bytesOf(const std::string& s)
        {
            return TadBytes(s.begin(), s.end());
        }

        /**
         * A whole wire packet whose single subpacket is the 192-byte player
         * info message: code and DirectPlay id set, every other byte zero.
         * The checksum is written by the encryption at the record level, and
         * the two bytes held for it start at zero.
         */
        TadBytes makeStatusPacket(uint32_t dplayId)
        {
            TadBytes packet;
            packet.reserve(StatusMessageOffset + StatusMessageSize);
            packet.push_back(TadPacketUncompressed);
            packet.push_back(0);
            packet.push_back(0);
            packet.insert(packet.end(), 4, 0);
            packet.resize(StatusMessageOffset + StatusMessageSize, 0);
            packet[StatusMessageOffset] = StatusMessageCode;
            writeU32(&packet[StatusMessageOffset + StatusDplayIdOffset], dplayId);
            return packet;
        }
    }

    TadWriter::TadWriter(std::ostream& out, TadWriterSettings settings)
        : stream(&out), settings(std::move(settings))
    {
    }

    TadWriter::TadWriter(const std::filesystem::path& path, TadWriterSettings settings)
        : file(std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::trunc)),
          stream(file.get()),
          settings(std::move(settings))
    {
        if (!*file)
        {
            throw TadException("failed to open demo file for writing: " + path.string());
        }
    }

    TadWriter::~TadWriter()
    {
        try
        {
            close();
        }
        catch (...)
        {
            // A destructor cannot report a failed flush; a caller that wants to
            // know calls close() itself.
        }
    }

    void TadWriter::writeHeader()
    {
        expectStage(Stage::Start, "header");

        if (settings.version != TadMaxVersion)
        {
            throw TadException("TadWriter writes version " + std::to_string(TadMaxVersion) + " demos only");
        }
        if (settings.numPlayers != settings.players.size())
        {
            throw TadException("numPlayers does not match the player table");
        }
        if (settings.mapName.size() > 64)
        {
            throw TadException("Map name is too long for a demo: " + std::to_string(settings.mapName.size()) + " bytes");
        }
        if (containsNul(settings.mapName))
        {
            throw TadException("Map name contains a NUL, which the reader would take for the end");
        }

        TadBytes payload(TadMagicNumber, TadMagicNumber + sizeof(TadMagicNumber));
        appendU16(payload, settings.version);
        payload.push_back(settings.numPlayers);
        appendU16(payload, settings.maxUnits);
        payload.insert(payload.end(), settings.mapName.begin(), settings.mapName.end());

        writeRecord(payload);
        stage = Stage::Header;
    }

    void TadWriter::writeExtraSectors()
    {
        expectStage(Stage::Header, "extra sectors");

        if (!settings.playerAddresses.empty() && settings.playerAddresses.size() != settings.players.size())
        {
            throw TadException("playerAddresses does not match the player table");
        }

        // Recorder version, date, and one address per player: the sectors demo
        // 15145 carries, and what the reader's de-obfuscation is checked
        // against.
        TadBytes extraHeader;
        appendU32(extraHeader, static_cast<uint32_t>(2 + settings.players.size()));
        writeRecord(extraHeader);

        writeSector(static_cast<uint32_t>(TadExtraSectorType::RecorderVersion), bytesOf(settings.recorderVersion));
        writeSector(static_cast<uint32_t>(TadExtraSectorType::Date), bytesOf(settings.date));

        for (std::size_t i = 0; i < settings.players.size(); ++i)
        {
            TadBytes address = i < settings.playerAddresses.size() ? bytesOf(settings.playerAddresses[i]) : TadBytes{};
            for (auto& b : address)
            {
                b ^= PlayerAddrXorKey;
            }
            writeSector(static_cast<uint32_t>(TadExtraSectorType::PlayerAddr), address);
        }

        stage = Stage::Sectors;
    }

    void TadWriter::writePlayers()
    {
        expectStage(Stage::Sectors, "player table");

        for (const auto& player : settings.players)
        {
            if (containsNul(player.name))
            {
                throw TadException("Player name contains a NUL, which the reader would take for the end");
            }

            TadBytes payload;
            payload.push_back(player.color);
            payload.push_back(static_cast<uint8_t>(player.side));
            payload.push_back(player.number);
            payload.insert(payload.end(), player.name.begin(), player.name.end());
            writeRecord(payload);
        }

        stage = Stage::Players;
    }

    void TadWriter::writePlayerStatuses()
    {
        expectStage(Stage::Players, "player statuses");

        if (!settings.dplayIds.empty() && settings.dplayIds.size() != settings.players.size())
        {
            throw TadException("dplayIds does not match the player table");
        }

        for (std::size_t i = 0; i < settings.players.size(); ++i)
        {
            auto dplayId = i < settings.dplayIds.size() ? settings.dplayIds[i] : 0u;

            // The message is a packet, so it compresses the way a packet does
            // and then encrypts with the player number in front of it -- offset
            // 1, where the key counter is not the absolute index.
            auto compressed = tadCompress(makeStatusPacket(dplayId));
            TadBytes record;
            record.reserve(compressed.size() + 1);
            record.push_back(settings.players[i].number);
            record.insert(record.end(), compressed.begin(), compressed.end());
            tadEncrypt(record, 1);
            writeRecord(record);
        }

        stage = Stage::Statuses;
    }

    void TadWriter::writeUnitTable(std::size_t unitTypeCount)
    {
        expectStage(Stage::Statuses, "unit table");

        if (unitTypeCount == 0)
        {
            throw TadException("A demo needs at least the pseudo-entry in its unit table");
        }
        // Divide rather than multiply, so an absurd count cannot wrap the
        // product before the comparison sees it.
        if (unitTypeCount > TadMaxRecordLength / (2 * UnitTableEntrySize))
        {
            throw TadException("Unit count does not fit one 0x1a record: " + std::to_string(unitTypeCount));
        }

        const uint32_t pseudoId = TadUnitTable::pseudoEntryId;

        // Both blocks ascend by id; the pseudo-entry takes its sorted place
        // among the running numbers, which is what the corpus shows.
        std::vector<uint32_t> ids;
        ids.reserve(unitTypeCount);
        for (std::size_t i = 0; i + 1 < unitTypeCount; ++i)
        {
            ids.push_back(SyntheticIdBase + static_cast<uint32_t>(i));
        }
        ids.push_back(pseudoId);
        std::sort(ids.begin(), ids.end());

        TadBytes record;
        record.reserve(unitTypeCount * 2 * UnitTableEntrySize);
        for (auto sub : {uint8_t{2}, uint8_t{3}})
        {
            for (auto id : ids)
            {
                bool isPseudo = id == pseudoId;
                record.push_back(static_cast<uint8_t>(TadSubPacketCode::UnitData));
                record.push_back(sub);
                record.insert(record.end(), 4, 0);
                appendU32(record, id);
                appendU32(record, isPseudo ? PseudoValue : UnitValue);
            }
        }

        writeRecord(record);
        stage = Stage::UnitTable;
    }

    void TadWriter::writePacket(uint16_t time, uint8_t sender, const std::vector<TadBytes>& subPackets)
    {
        if (stage != Stage::UnitTable && stage != Stage::Packets)
        {
            throw TadException("Cannot write a packet now: the demo's records go in a fixed order");
        }

        TadBytes payload{TadPacketUncompressed};
        for (const auto& subPacket : subPackets)
        {
            payload.insert(payload.end(), subPacket.begin(), subPacket.end());
        }

        TadBytes record;
        record.reserve(payload.size() + sizeof(TadPacketPrefix));
        appendU16(record, time);
        record.push_back(sender);
        record.insert(record.end(), payload.begin(), payload.end());

        writeRecord(record);

        // Flushed per packet: a demo is most wanted for the game that ended in
        // a crash, and the same reasoning is on ReplayWriter.
        stream->flush();

        stage = Stage::Packets;
    }

    void TadWriter::close()
    {
        if (stage == Stage::Closed)
        {
            return;
        }

        stream->flush();
        if (!*stream)
        {
            throw TadException("failed to flush the demo");
        }
        if (file)
        {
            file->close();
        }

        stage = Stage::Closed;
    }

    void TadWriter::expectStage(Stage expected, const char* what)
    {
        if (stage != expected)
        {
            throw TadException(std::string("Cannot write the ") + what + " now: the demo's records go in a fixed order");
        }
    }

    void TadWriter::writeRecord(const TadBytes& payload)
    {
        auto length = payload.size() + 2;
        if (length < TadMinRecordLength || length > TadMaxRecordLength)
        {
            throw TadException("Demo record does not fit the two-byte length: "
                + std::to_string(payload.size()) + " bytes");
        }

        TadBytes record;
        record.reserve(length);
        appendU16(record, static_cast<uint16_t>(length));
        record.insert(record.end(), payload.begin(), payload.end());

        stream->write(reinterpret_cast<const char*>(record.data()), static_cast<std::streamsize>(record.size()));
        if (!*stream)
        {
            throw TadException("failed to write demo record");
        }
    }

    void TadWriter::writeSector(uint32_t type, const TadBytes& data)
    {
        TadBytes payload;
        appendU32(payload, type);
        payload.insert(payload.end(), data.begin(), data.end());
        writeRecord(payload);
    }
}
