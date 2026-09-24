#include <catch2/catch_test_macros.hpp>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/TadWriter.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tad/tad_headers.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace rwe
{
    namespace
    {
        namespace fs = std::filesystem;

        TadWriterSettings makeSettings()
        {
            TadWriterSettings settings;
            settings.numPlayers = 2;
            settings.maxUnits = 1000;
            settings.mapName = "[100] Red Hills Mining Town";
            settings.recorderVersion = "rwe-test";
            settings.date = "2026-09-24";
            settings.players = {
                TadPlayer{1, static_cast<int8_t>(TadSide::Arm), 1, "PanchoU"},
                TadPlayer{2, static_cast<int8_t>(TadSide::Watch), 2, "Adwel91"},
            };
            settings.dplayIds = {0x11223344u, 0x55667788u};
            settings.playerAddresses = {"127.0.0.1", "::1"};
            return settings;
        }

        /** The four records every demo opens with, in the order the reader needs. */
        void writePrefix(TadWriter& writer)
        {
            writer.writeHeader();
            writer.writeExtraSectors();
            writer.writePlayers();
            writer.writePlayerStatuses();
        }

        std::string writeEmptyDemo(const TadWriterSettings& settings, std::size_t unitTypeCount)
        {
            std::ostringstream stream;
            TadWriter writer(stream, settings);
            writePrefix(writer);
            writer.writeUnitTable(unitTypeCount);
            writer.close();
            return stream.str();
        }

        struct RecordingHandler : TadHandler
        {
            std::optional<TadHeader> header;
            std::vector<TadExtraSector> extraSectors;
            std::vector<TadPlayer> players;
            std::vector<TadPlayerStatus> statuses;
            std::vector<TadBytes> unitData;
            std::vector<TadPacket> packets;
            std::vector<std::vector<TadBytes>> subPackets;
            TadWalkStats stats;

            void onHeader(const TadHeader& h) override { header = h; }
            void onExtraSector(const TadExtraSector& s, unsigned int, unsigned int) override { extraSectors.push_back(s); }
            void onPlayer(const TadPlayer& p, unsigned int, unsigned int) override { players.push_back(p); }
            void onPlayerStatus(const TadPlayerStatus& s, unsigned int, unsigned int) override { statuses.push_back(s); }
            void onUnitData(const TadBytes& d) override { unitData.push_back(d); }

            void onPacket(const TadPacket& p, const std::vector<TadBytes>& subs, const TadWalkStats& s) override
            {
                packets.push_back(p);
                subPackets.push_back(subs);
                stats.unknownCodes += s.unknownCodes;
                stats.truncated += s.truncated;
                stats.failedDecompressions += s.failedDecompressions;
            }
        };

        RecordingHandler readDemo(const std::string& demo)
        {
            std::istringstream stream(demo);
            RecordingHandler handler;
            readTad(stream, handler);
            return handler;
        }

        std::string stringOf(const TadBytes& b)
        {
            return std::string(b.begin(), b.end());
        }

        fs::path uniqueTempPath()
        {
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            return fs::temp_directory_path() / ("rwe-tad-writer-test-" + std::to_string(stamp) + ".tad");
        }

        /** Deletes its file however the test leaves, passing or failing. */
        struct TempFile
        {
            fs::path path{uniqueTempPath()};

            ~TempFile()
            {
                std::error_code ec;
                fs::remove(path, ec);
            }
        };
    }

    TEST_CASE("TadWriter header", "[tad]")
    {
        SECTION("round-trips the fields the header carries")
        {
            auto handler = readDemo(writeEmptyDemo(makeSettings(), 10));

            REQUIRE(handler.header.has_value());
            REQUIRE(handler.header->version == 5);
            REQUIRE(handler.header->numPlayers == 2);
            REQUIRE(handler.header->maxUnits == 1000);
            REQUIRE(handler.header->mapName == "[100] Red Hills Mining Town");
        }

        SECTION("frames the header with a length that counts its own two bytes")
        {
            // The header's fixed part is 13 bytes and the map name runs to the
            // end of the record, with no terminator: demo 14719's is 42 bytes
            // for a 27-character name, which is what tad.test.cpp's realPrefix
            // pins.
            auto settings = makeSettings();
            settings.mapName = "Small map";
            auto demo = writeEmptyDemo(settings, 1);

            auto length = static_cast<std::size_t>(static_cast<uint8_t>(demo[0]))
                | (static_cast<std::size_t>(static_cast<uint8_t>(demo[1])) << 8);
            REQUIRE(length == 2 + sizeof(TadHeaderPrefix) + settings.mapName.size());

            REQUIRE(demo.compare(2, sizeof(TadMagicNumber), std::string(TadMagicNumber, sizeof(TadMagicNumber))) == 0);
            REQUIRE(stringOf(TadBytes(demo.begin() + 2 + sizeof(TadHeaderPrefix), demo.begin() + length)) == "Small map");
        }

        SECTION("rejects a version whose sectors the reader would not expect")
        {
            auto settings = makeSettings();
            settings.version = 4;

            std::ostringstream stream;
            TadWriter writer(stream, settings);
            REQUIRE_THROWS_AS(writer.writeHeader(), TadException);
        }

        SECTION("rejects a player count that disagrees with the table")
        {
            auto settings = makeSettings();
            settings.numPlayers = 3;

            std::ostringstream stream;
            TadWriter writer(stream, settings);
            REQUIRE_THROWS_AS(writer.writeHeader(), TadException);
        }

        SECTION("rejects a map name the reader would refuse")
        {
            auto tooLong = makeSettings();
            tooLong.mapName = std::string(65, 'x'); // past the reader's 64-byte bound

            std::ostringstream longStream;
            TadWriter longWriter(longStream, tooLong);
            REQUIRE_THROWS_AS(longWriter.writeHeader(), TadException);

            // The reader reads a trailing string to the record's end or the
            // first NUL, so a NUL would silently shorten the name.
            auto withNul = makeSettings();
            withNul.mapName = std::string("a\0b", 3);

            std::ostringstream nulStream;
            TadWriter nulWriter(nulStream, withNul);
            REQUIRE_THROWS_AS(nulWriter.writeHeader(), TadException);
        }
    }

    TEST_CASE("TadWriter extra sectors", "[tad]")
    {
        SECTION("writes the recorder version and the date")
        {
            auto handler = readDemo(writeEmptyDemo(makeSettings(), 10));

            REQUIRE(handler.extraSectors.size() == 4);
            REQUIRE(handler.extraSectors[0].type == static_cast<uint32_t>(TadExtraSectorType::RecorderVersion));
            REQUIRE(stringOf(handler.extraSectors[0].data) == "rwe-test");
            REQUIRE(handler.extraSectors[1].type == static_cast<uint32_t>(TadExtraSectorType::Date));
            REQUIRE(stringOf(handler.extraSectors[1].data) == "2026-09-24");

            // The two injected fields reach the file, so a test can be
            // byte-stable without freezing a clock.
            auto other = makeSettings();
            other.date = "2000-01-01";
            REQUIRE(writeEmptyDemo(makeSettings(), 10) != writeEmptyDemo(other, 10));
        }

        SECTION("writes one address sector per player, obfuscated")
        {
            // Demo 15145 carries one type-6 sector per player, and XOR 42 is
            // what the reader undoes.
            auto demo = writeEmptyDemo(makeSettings(), 10);
            auto handler = readDemo(demo);

            REQUIRE(handler.extraSectors.size() == 4);
            REQUIRE(handler.extraSectors[2].type == static_cast<uint32_t>(TadExtraSectorType::PlayerAddr));
            REQUIRE(stringOf(handler.extraSectors[2].data) == "127.0.0.1");
            REQUIRE(handler.extraSectors[3].type == static_cast<uint32_t>(TadExtraSectorType::PlayerAddr));
            REQUIRE(stringOf(handler.extraSectors[3].data) == "::1");

            REQUIRE(demo.find("127.0.0.1") == std::string::npos);

            std::string obfuscated = "127.0.0.1";
            for (auto& c : obfuscated)
            {
                c = static_cast<char>(static_cast<uint8_t>(c) ^ 42);
            }
            REQUIRE(demo.find(obfuscated) != std::string::npos);
        }

        SECTION("still writes a sector for a player with no address")
        {
            auto settings = makeSettings();
            settings.playerAddresses.clear();
            auto handler = readDemo(writeEmptyDemo(settings, 10));

            REQUIRE(handler.extraSectors.size() == 4);
            REQUIRE(handler.extraSectors[2].type == static_cast<uint32_t>(TadExtraSectorType::PlayerAddr));
            REQUIRE(handler.extraSectors[2].data.empty());
        }

        SECTION("rejects an address list that disagrees with the player table")
        {
            auto settings = makeSettings();
            settings.playerAddresses = {"127.0.0.1"};

            std::ostringstream stream;
            TadWriter writer(stream, settings);
            writer.writeHeader();
            REQUIRE_THROWS_AS(writer.writeExtraSectors(), TadException);
        }
    }

    TEST_CASE("TadWriter players", "[tad]")
    {
        SECTION("round-trips the player table")
        {
            auto handler = readDemo(writeEmptyDemo(makeSettings(), 10));

            REQUIRE(handler.players.size() == 2);
            REQUIRE(handler.players[0].color == 1);
            REQUIRE(handler.players[0].side == static_cast<int8_t>(TadSide::Arm));
            REQUIRE(handler.players[0].number == 1);
            REQUIRE(handler.players[0].name == "PanchoU");
            REQUIRE_FALSE(handler.players[0].isWatcher());

            REQUIRE(handler.players[1].name == "Adwel91");
            REQUIRE(handler.players[1].isWatcher());
        }

        SECTION("rejects a name with a NUL in it")
        {
            auto settings = makeSettings();
            settings.players[0].name = std::string("a\0b", 3);

            std::ostringstream stream;
            TadWriter writer(stream, settings);
            writer.writeHeader();
            writer.writeExtraSectors();
            REQUIRE_THROWS_AS(writer.writePlayers(), TadException);
        }
    }

    TEST_CASE("TadWriter player statuses", "[tad]")
    {
        SECTION("puts the DirectPlay id where the reader looks and verifies the checksum")
        {
            auto handler = readDemo(writeEmptyDemo(makeSettings(), 10));

            REQUIRE(handler.statuses.size() == 2);
            REQUIRE(handler.statuses[0].number == 1);
            REQUIRE(handler.statuses[0].checksum.matches());
            REQUIRE(handler.statuses[0].dplayId == 0x11223344);
            REQUIRE(handler.statuses[1].number == 2);
            REQUIRE(handler.statuses[1].checksum.matches());
            REQUIRE(handler.statuses[1].dplayId == 0x55667788);
        }

        SECTION("writes the 192-byte message the corpus records")
        {
            // Every one of the 89 status messages in the corpus left the reader
            // 192 bytes after the seven-byte wire preamble was dropped, which
            // is what makes this the lobby's long-form 0x20 and puts the id at
            // 0x91. The code and the id are all the writer knows, so the rest is
            // zero.
            auto handler = readDemo(writeEmptyDemo(makeSettings(), 10));

            auto messageFor = [](uint32_t dplayId) {
                TadBytes message(192, 0);
                message[0] = static_cast<uint8_t>(TadSubPacketCode::PlayerInfo);
                for (std::size_t i = 0; i < 4; ++i)
                {
                    message[0x91 + i] = static_cast<uint8_t>(dplayId >> (8 * i));
                }
                return message;
            };

            REQUIRE(handler.statuses[0].statusMessage.size() == 192);
            REQUIRE(handler.statuses[0].statusMessage == messageFor(0x11223344));
            REQUIRE(handler.statuses[1].statusMessage == messageFor(0x55667788));
        }

        SECTION("zero-fills the ids when none are given")
        {
            auto settings = makeSettings();
            settings.dplayIds.clear();
            auto handler = readDemo(writeEmptyDemo(settings, 10));

            REQUIRE(handler.statuses[0].dplayId == 0);
            REQUIRE(handler.statuses[1].dplayId == 0);
        }

        SECTION("rejects an id list that disagrees with the player table")
        {
            auto settings = makeSettings();
            settings.dplayIds = {0x11223344u};

            std::ostringstream stream;
            TadWriter writer(stream, settings);
            writer.writeHeader();
            writer.writeExtraSectors();
            writer.writePlayers();
            REQUIRE_THROWS_AS(writer.writePlayerStatuses(), TadException);
        }
    }

    TEST_CASE("TadWriter unit table", "[tad]")
    {
        SECTION("writes the restricted count and the pseudo-entry in both blocks")
        {
            auto handler = readDemo(writeEmptyDemo(makeSettings(), 317));

            REQUIRE(handler.unitData.size() == 1);
            auto table = tadDecodeUnitTable(handler.unitData[0]);
            REQUIRE(table);

            // 317 is ProTA 4.8's published count, and the restricted block is
            // exactly that -- tad_episodes compares it against --units. The
            // pseudo-entry sits inside it, being the one id that data set and
            // TA: Escalation share.
            REQUIRE(table->restricted.size() == 317);
            REQUIRE(table->listed.size() == 317);

            auto isPseudo = [](const TadUnitTableEntry& e) { return e.id == TadUnitTable::pseudoEntryId; };
            REQUIRE(std::any_of(table->restricted.begin(), table->restricted.end(), isPseudo));
            REQUIRE(std::any_of(table->listed.begin(), table->listed.end(), isPseudo));

            REQUIRE(std::is_sorted(table->restricted.begin(), table->restricted.end(), [](const auto& a, const auto& b) {
                return a.id < b.id;
            }));
            REQUIRE(std::is_sorted(table->listed.begin(), table->listed.end(), [](const auto& a, const auto& b) {
                return a.id < b.id;
            }));

            for (const auto& entry : table->restricted)
            {
                // The packed value is three fields: low byte 1, flag byte, and
                // the 0xffff sentinel -- cleared only on the pseudo-entry.
                REQUIRE(entry.value == (isPseudo(entry) ? 0xffff0001u : 0xffff0101u));
            }
        }

        SECTION("is byte-stable for a given count")
        {
            REQUIRE(writeEmptyDemo(makeSettings(), 317) == writeEmptyDemo(makeSettings(), 317));
        }

        SECTION("rejects a count that cannot be one record")
        {
            for (auto count : {std::size_t{0}, std::size_t{2000}})
            {
                std::ostringstream stream;
                TadWriter writer(stream, makeSettings());
                writePrefix(writer);
                REQUIRE_THROWS_AS(writer.writeUnitTable(count), TadException);
            }
        }
    }

    TEST_CASE("TadWriter packets", "[tad]")
    {
        SECTION("round-trips a packet and its subpackets")
        {
            // Subpackets the length table can size: a wrong concatenation
            // shows up as a walk out of step rather than as a wrong byte
            // count, which is the failure the table exists to catch.
            TadBytes speed{0x19, 0x00, 0x01};
            TadBytes damage(9, 0x0b);
            TadBytes buildFinished(5, 0x12);

            std::ostringstream stream;
            TadWriter writer(stream, makeSettings());
            writePrefix(writer);
            writer.writeUnitTable(10);
            writer.writePacket(100, 2, {speed, damage});
            writer.writePacket(250, 1, {buildFinished});
            writer.close();

            auto handler = readDemo(stream.str());

            REQUIRE(handler.unitData.size() == 1);
            REQUIRE(handler.packets.size() == 2);
            REQUIRE(handler.packets[0].time == 100);
            REQUIRE(handler.packets[0].sender == 2);
            REQUIRE(handler.subPackets[0] == std::vector<TadBytes>{speed, damage});
            REQUIRE(handler.packets[1].time == 250);
            REQUIRE(handler.packets[1].sender == 1);
            REQUIRE(handler.subPackets[1] == std::vector<TadBytes>{buildFinished});
            REQUIRE(handler.stats.total() == 0);
        }

        SECTION("rejects a packet before the unit table")
        {
            std::ostringstream stream;
            TadWriter writer(stream, makeSettings());
            writePrefix(writer);
            REQUIRE_THROWS_AS(writer.writePacket(0, 1, {}), TadException);
        }
    }

    TEST_CASE("TadWriter record order", "[tad]")
    {
        SECTION("rejects the header twice")
        {
            std::ostringstream stream;
            TadWriter writer(stream, makeSettings());
            writer.writeHeader();
            REQUIRE_THROWS_AS(writer.writeHeader(), TadException);
        }

        SECTION("rejects writing after close")
        {
            std::ostringstream stream;
            TadWriter writer(stream, makeSettings());
            writePrefix(writer);
            writer.writeUnitTable(10);
            writer.close();
            REQUIRE_THROWS_AS(writer.writePacket(0, 1, {}), TadException);
        }
    }

    TEST_CASE("TadWriter path constructor", "[tad]")
    {
        TempFile file;

        {
            TadWriter writer(file.path, makeSettings());
            writePrefix(writer);
            writer.writeUnitTable(10);
            writer.close();
        }

        std::ifstream in(file.path, std::ios::binary);
        REQUIRE(in.is_open());
        std::ostringstream buffer;
        buffer << in.rdbuf();

        auto handler = readDemo(buffer.str());
        REQUIRE(handler.header.has_value());
        REQUIRE(handler.header->mapName == "[100] Red Hills Mining Town");
        REQUIRE(handler.statuses.size() == 2);
        REQUIRE(handler.unitData.size() == 1);
    }
}
