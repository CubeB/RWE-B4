#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_util.h>
#include <rwe/util/SpanStream.h>

#include <numeric>
#include <sstream>

namespace rwe
{
    namespace
    {
        /** A packet payload with the three-byte header the compressor assumes. */
        TadBytes withHeader(std::initializer_list<uint8_t> body)
        {
            TadBytes data{TadPacketUncompressed, 0x00, 0x00};
            data.insert(data.end(), body);
            return data;
        }

        TadBytes roundTrip(const TadBytes& data)
        {
            bool ok = false;
            auto result = tadDecompress(tadCompress(data), 3, ok);
            REQUIRE(ok);
            return result;
        }

        void appendRecord(TadBytes& out, const TadBytes& payload)
        {
            auto length = payload.size() + 2;
            out.push_back(static_cast<uint8_t>(length));
            out.push_back(static_cast<uint8_t>(length >> 8));
            out.insert(out.end(), payload.begin(), payload.end());
        }

        TadBytes bytesOf(const std::string& s)
        {
            return TadBytes(s.begin(), s.end());
        }

        std::string stringOf(const TadBytes& b)
        {
            return std::string(b.begin(), b.end());
        }

        /**
         * A copy with the two checksum bytes blanked. Encryption writes them and
         * decryption only reads them, so they are the one part of a packet that
         * does not survive a round trip unchanged.
         */
        TadBytes withoutChecksum(const TadBytes& data, std::size_t ofs)
        {
            auto result = data;
            result[ofs + 1] = 0;
            result[ofs + 2] = 0;
            return result;
        }

        /**
         * The first 83 bytes of a real recording, verbatim: demo 14719 from
         * tademos.xyz, written by the TA Forever recorder. Four complete records
         * -- the header, the extra header, and the recorder-version and date
         * sectors. Everything after them in this test is synthesised, because a
         * demo must not be checked in; these 83 bytes are here so that the
         * numbers the reader is asserted against are real ones.
         */
        const TadBytes realPrefix{
            // header record, length 42
            0x2a,
            0x00,
            'T',
            'A',
            ' ',
            'D',
            'e',
            'm',
            'o',
            0x00, // magic
            0x05,
            0x00, // version 5
            0x02, // 2 players
            0xe8,
            0x03, // maxUnits 1000
            // map name, offset 13, running to the end of the record with no NUL
            '[',
            '1',
            '0',
            '0',
            ']',
            ' ',
            'R',
            'e',
            'd',
            ' ',
            'H',
            'i',
            'l',
            'l',
            's',
            ' ',
            'M',
            'i',
            'n',
            'i',
            'n',
            'g',
            ' ',
            'T',
            'o',
            'w',
            'n',

            // extra header record, length 6: four sectors follow
            0x06,
            0x00,
            0x04,
            0x00,
            0x00,
            0x00,

            // extra sector, length 19: type 3 (recorder version)
            0x13,
            0x00,
            0x03,
            0x00,
            0x00,
            0x00,
            't',
            'a',
            'f',
            '-',
            '2',
            '0',
            '2',
            '6',
            '.',
            '8',
            '.',
            '1',
            '5',

            // extra sector, length 16: type 4 (date)
            0x10,
            0x00,
            0x04,
            0x00,
            0x00,
            0x00,
            '2',
            '0',
            '2',
            '6',
            '-',
            '0',
            '9',
            '-',
            '0',
            '7'};

        /**
         * A player status record whose decoded message is long enough to carry a
         * DirectPlay id at 0x91. Plain (0x03), so it survives decompression
         * untouched; the body is filled with a marker so that a mis-sized skip
         * shows up as a wrong id rather than as a crash.
         */
        TadBytes makeStatusRecord(uint8_t playerNumber, uint32_t dplayId)
        {
            TadBytes packet{TadPacketUncompressed, 0x00, 0x00};

            // Seven bytes of DirectPlay framing, then the message itself. The
            // reader drops those seven, so the id it looks for at 0x91 sits at
            // 7 + 0x91 counting from the front of the packet.
            packet.insert(packet.end(), 7, 0xaa);
            packet.insert(packet.end(), 0x91 + 4, 0xbb);

            auto idOffset = 7 + 0x91;
            packet[idOffset + 0] = static_cast<uint8_t>(dplayId);
            packet[idOffset + 1] = static_cast<uint8_t>(dplayId >> 8);
            packet[idOffset + 2] = static_cast<uint8_t>(dplayId >> 16);
            packet[idOffset + 3] = static_cast<uint8_t>(dplayId >> 24);

            // The record carries the player number ahead of the packet, which is
            // why the encryption starts one byte in.
            TadBytes record{playerNumber};
            record.insert(record.end(), packet.begin(), packet.end());
            tadEncrypt(record, 1);
            return record;
        }

        /**
         * The real prefix above, completed with the minimum a reader needs: the
         * two remaining extra sectors the header promises, the player table, the
         * status messages and the unit data record.
         */
        TadBytes makeDemo(const std::vector<TadBytes>& packetRecords = {})
        {
            TadBytes demo = realPrefix;

            // sector 7: the mod id, which is what identifies a non-vanilla game
            TadBytes modId{0x07, 0x00, 0x00, 0x00};
            auto modName = bytesOf("Balanced Annihilation");
            modId.insert(modId.end(), modName.begin(), modName.end());
            appendRecord(demo, modId);

            // sector 6: the player addresses, obfuscated with XOR 42
            TadBytes playerAddr{0x06, 0x00, 0x00, 0x00};
            for (auto c : bytesOf("hi"))
            {
                playerAddr.push_back(c ^ 42);
            }
            appendRecord(demo, playerAddr);

            TadBytes playerOne{0x01, static_cast<uint8_t>(TadSide::Arm), 0x02};
            auto nameOne = bytesOf("PanchoU");
            playerOne.insert(playerOne.end(), nameOne.begin(), nameOne.end());
            appendRecord(demo, playerOne);

            TadBytes playerTwo{0x02, static_cast<uint8_t>(TadSide::Watch), 0x03};
            auto nameTwo = bytesOf("Adwel91");
            playerTwo.insert(playerTwo.end(), nameTwo.begin(), nameTwo.end());
            appendRecord(demo, playerTwo);

            appendRecord(demo, makeStatusRecord(0x02, 0x11223344));
            appendRecord(demo, makeStatusRecord(0x03, 0x55667788));

            // the 0x1a unit type table, whose contents this change does not read
            appendRecord(demo, TadBytes{0x1a, 0x02, 0x00, 0x00});

            for (const auto& packet : packetRecords)
            {
                appendRecord(demo, packet);
            }

            return demo;
        }

        /** A packet record: time, sender, then a plain payload of subpackets. */
        TadBytes makePacketRecord(uint16_t time, uint8_t sender, const TadBytes& subPackets)
        {
            TadBytes record{
                static_cast<uint8_t>(time),
                static_cast<uint8_t>(time >> 8),
                sender,
                TadPacketUncompressed};
            record.insert(record.end(), subPackets.begin(), subPackets.end());
            return record;
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

        RecordingHandler readDemo(const TadBytes& demo)
        {
            SpanStream stream(reinterpret_cast<const char*>(demo.data()), demo.size());
            RecordingHandler handler;
            readTad(stream, handler);
            return handler;
        }
    }

    TEST_CASE("tadDecompress", "[tad]")
    {
        SECTION("passes an uncompressed packet through untouched")
        {
            auto data = withHeader({1, 2, 3, 4, 5});
            bool ok = false;
            REQUIRE(tadDecompress(data, 3, ok) == data);
            REQUIRE(ok);
        }

        SECTION("round-trips a payload with no repetition in it")
        {
            // Nothing to match, so every slot comes out a literal.
            auto data = withHeader({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13});
            REQUIRE(roundTrip(data) == data);
        }

        SECTION("round-trips a long run")
        {
            // The self-referential copy: the source catches the cursor and the
            // run repeats out of the output buffer as it is being written.
            TadBytes data{TadPacketUncompressed, 0x00, 0x00};
            data.insert(data.end(), 300, 0x5a);
            REQUIRE(roundTrip(data) == data);
        }

        SECTION("round-trips a match at the longest length the format can encode")
        {
            // Seventeen bytes is (0x0f + 2), the largest a four-bit length holds.
            TadBytes pattern(17);
            std::iota(pattern.begin(), pattern.end(), uint8_t{1});

            TadBytes data{TadPacketUncompressed, 0x00, 0x00};
            data.insert(data.end(), 40, 0x00); // push the match out past index 6
            data.insert(data.end(), pattern.begin(), pattern.end());
            data.insert(data.end(), pattern.begin(), pattern.end());
            REQUIRE(roundTrip(data) == data);
        }

        SECTION("round-trips a payload built from a repeating dictionary")
        {
            // The shape that actually exercises the back-reference path: random
            // bytes compress to nothing but literals and prove very little.
            std::vector<TadBytes> dictionary{
                bytesOf("the quick brown fox"),
                bytesOf("jumps over"),
                bytesOf("the lazy dog"),
                TadBytes{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}};

            TadBytes data{TadPacketUncompressed, 0x00, 0x00};
            for (int i = 0; i < 40; ++i)
            {
                const auto& symbol = dictionary[i % dictionary.size()];
                data.insert(data.end(), symbol.begin(), symbol.end());
            }

            auto compressed = tadCompress(data);
            REQUIRE(compressed[0] == TadPacketCompressed);
            REQUIRE(compressed.size() < data.size());
            REQUIRE(roundTrip(data) == data);
        }

        SECTION("terminates on a zero offset")
        {
            // One literal, then a back-reference whose offset is zero.
            TadBytes compressed{TadPacketCompressed, 0x00, 0x00, 0b00000010, 0x77, 0x00, 0x00};
            bool ok = false;
            auto result = tadDecompress(compressed, 3, ok);
            REQUIRE(ok);
            REQUIRE(result == withHeader({0x77}));
        }

        SECTION("reports a stream that runs out mid-block")
        {
            // The control byte promises eight slots and the input supplies one.
            TadBytes compressed{TadPacketCompressed, 0x00, 0x00, 0x00, 0x77};
            bool ok = true;
            tadDecompress(compressed, 3, ok);
            REQUIRE_FALSE(ok);
        }

        SECTION("reports a back-reference with only one of its two bytes")
        {
            TadBytes compressed{TadPacketCompressed, 0x00, 0x00, 0b00000001, 0x10};
            bool ok = true;
            tadDecompress(compressed, 3, ok);
            REQUIRE_FALSE(ok);
        }

        SECTION("round-trips arbitrary payloads assembled from a dictionary")
        {
            rc::prop("compress followed by decompress is the identity", [](const std::vector<uint8_t>& symbolIndices) {
                std::vector<TadBytes> dictionary{
                    TadBytes{0x11},
                    TadBytes{0x22, 0x33},
                    TadBytes{0x44, 0x55, 0x66, 0x77, 0x88},
                    TadBytes{0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99},
                    bytesOf("ARMCOM"),
                    bytesOf("a longer symbol, long enough to be worth a match")};

                TadBytes data{TadPacketUncompressed, 0x00, 0x00};
                for (auto index : symbolIndices)
                {
                    const auto& symbol = dictionary[index % dictionary.size()];
                    data.insert(data.end(), symbol.begin(), symbol.end());
                }

                bool ok = false;
                auto result = tadDecompress(tadCompress(data), 3, ok);
                RC_ASSERT(ok);
                RC_ASSERT(result == data);
            });
        }
    }

    TEST_CASE("tadDecrypt", "[tad]")
    {
        SECTION("round-trips at offset zero and restores the checksum")
        {
            TadBytes data{0x03, 0x00, 0x00, 10, 20, 30, 40, 50, 60, 70};
            auto plain = data;

            tadEncrypt(data, 0);
            REQUIRE(data != plain);

            auto check = tadDecrypt(data, 0);
            REQUIRE(withoutChecksum(data, 0) == plain);
            REQUIRE(check.matches());
        }

        SECTION("round-trips at the offset a player status record uses")
        {
            // The player number occupies byte 0, so the key counter starts one
            // byte later than the absolute index -- which is the reading the
            // reference documents wrongly.
            TadBytes data{0x07, 0x03, 0x00, 0x00, 10, 20, 30, 40, 50, 60, 70};
            auto plain = data;

            tadEncrypt(data, 1);
            REQUIRE(data != plain);
            REQUIRE(data[0] == 0x07);

            auto check = tadDecrypt(data, 1);
            REQUIRE(withoutChecksum(data, 1) == plain);
            REQUIRE(check.matches());
        }

        SECTION("leaves the bytes outside the body alone")
        {
            TadBytes data{0x03, 0x00, 0x00, 1, 2, 3, 4, 5, 6, 7};
            auto plain = data;
            tadEncrypt(data, 0);

            // Type byte, the two checksum bytes, and the last three bytes.
            REQUIRE(data[0] == plain[0]);
            REQUIRE(data[7] == plain[7]);
            REQUIRE(data[8] == plain[8]);
            REQUIRE(data[9] == plain[9]);
        }

        SECTION("notices a corrupted body")
        {
            TadBytes data{0x03, 0x00, 0x00, 10, 20, 30, 40, 50, 60, 70};
            tadEncrypt(data, 0);
            data[4] ^= 0xff;
            REQUIRE_FALSE(tadDecrypt(data, 0).matches());
        }

        SECTION("pads a packet too short to hold a header")
        {
            TadBytes data{0x03, 0x00};
            tadDecrypt(data, 0);
            REQUIRE(data == TadBytes{0x03, 0x00, 0x06});
        }
    }

    TEST_CASE("tadExpectedSubPacketSize", "[tad]")
    {
        auto sizeOf = [](const TadBytes& b) { return tadExpectedSubPacketSize(b.data(), b.size()); };

        SECTION("sizes the fixed-length codes")
        {
            REQUIRE(sizeOf(TadBytes(23, 0x09)) == 23); // build started
            REQUIRE(sizeOf(TadBytes(9, 0x0b)) == 9);   // damage
            REQUIRE(sizeOf(TadBytes(11, 0x0c)) == 11); // death
            REQUIRE(sizeOf(TadBytes(36, 0x0d)) == 36); // shot fired
            REQUIRE(sizeOf(TadBytes(5, 0x12)) == 5);   // build finished
            REQUIRE(sizeOf(TadBytes(3, 0x19)) == 3);   // speed
            REQUIRE(sizeOf(TadBytes(14, 0x1a)) == 14); // unit type data
            REQUIRE(sizeOf(TadBytes(58, 0x28)) == 58); // resource statistics
        }

        SECTION("sizes the in-stream player info at 186, not 192")
        {
            // The reference's table says 192, which is the length of the lobby
            // status packet the demo header carries -- not of the one the packet
            // stream carries. Walking the stream at 192 eats six bytes of the
            // alliance declarations that follow it. Measured across thirteen
            // games: 1123 occurrences, every one of them 186.
            REQUIRE(sizeOf(TadBytes(200, 0x20)) == 186);
        }

        SECTION("measures a run of zeros")
        {
            REQUIRE(sizeOf(TadBytes{0, 0, 0, 0x09}) == 3);
            REQUIRE(sizeOf(TadBytes{0, 0, 0}) == 3);
        }

        SECTION("takes the unit state length from the packet")
        {
            REQUIRE(sizeOf(TadBytes{0x2c, 0x0b, 0x00}) == 11);
            REQUIRE(sizeOf(TadBytes{0x2c, 0x00, 0x01}) == 256);
        }

        SECTION("takes the recorder data length from the byte after the code")
        {
            REQUIRE(sizeOf(TadBytes{0xfb, 0x05}) == 8);
        }

        SECTION("takes four off the coalesced tick length")
        {
            REQUIRE(sizeOf(TadBytes{0xfd, 0x0f, 0x00}) == 11);
        }

        SECTION("does not underflow on a coalesced tick that declares too little")
        {
            REQUIRE(sizeOf(TadBytes{0xfd, 0x02, 0x00}) == 0);
        }

        SECTION("sizes a chat message at 65 when it is terminated")
        {
            TadBytes chat(65, 'a');
            chat[0] = 0x05;
            chat[64] = 0;
            REQUIRE(sizeOf(chat) == 65);
        }

        SECTION("takes the whole buffer for an overlong chat message")
        {
            // Older recorders emit more text than they should, but always as a
            // single packet.
            TadBytes chat(80, 'a');
            chat[0] = 0x05;
            REQUIRE(sizeOf(chat) == 80);
        }

        SECTION("gives back the trailing map position of an overlong chat message")
        {
            TadBytes chat(80, 'a');
            chat[0] = 0x05;
            chat[75] = 0xfc;
            REQUIRE(sizeOf(chat) == 75);
        }

        SECTION("returns zero for a code it does not know")
        {
            // 0x13 is named in the reference but carries no size there, so it is
            // deliberately absent rather than guessed at.
            REQUIRE(sizeOf(TadBytes{0x13, 0, 0, 0}) == 0);
            REQUIRE(sizeOf(TadBytes{0x77, 0, 0, 0}) == 0);
            REQUIRE(sizeOf(TadBytes{}) == 0);
        }
    }

    TEST_CASE("tadSplitSubPackets", "[tad]")
    {
        SECTION("walks a concatenation of subpackets")
        {
            TadBytes payload;
            payload.insert(payload.end(), 3, 0x19); // speed, 3 bytes
            payload.insert(payload.end(), 9, 0x0b); // damage, 9 bytes
            payload.insert(payload.end(), {0x2c, 0x05, 0x00, 0xaa, 0xbb});
            payload.insert(payload.end(), 5, 0x12); // build finished, 5 bytes

            TadWalkStats stats;
            auto result = tadSplitSubPackets(payload.data(), payload.size(), stats);

            REQUIRE(stats.total() == 0);
            REQUIRE(result.size() == 4);
            REQUIRE(result[0].size() == 3);
            REQUIRE(result[1].size() == 9);
            REQUIRE(result[2].size() == 5);
            REQUIRE(result[3].size() == 5);
        }

        SECTION("reports an unknown code rather than absorbing it")
        {
            TadBytes payload{0x77, 0x01, 0x02, 0x03};
            TadWalkStats stats;
            auto result = tadSplitSubPackets(payload.data(), payload.size(), stats);

            REQUIRE(stats.unknownCodes == 1);
            REQUIRE(result.size() == 1);
            REQUIRE(result[0].size() == 4);
        }

        SECTION("reports a subpacket that runs past the end of the buffer")
        {
            TadBytes payload(4, 0x0b); // damage claims 9 bytes, 4 are present
            TadWalkStats stats;
            auto result = tadSplitSubPackets(payload.data(), payload.size(), stats);

            REQUIRE(stats.truncated == 1);
            REQUIRE(result.size() == 1);
        }
    }

    TEST_CASE("tadUnsmartpak", "[tad]")
    {
        SECTION("passes a demo recorded before SmartPak through unchanged")
        {
            TadBytes payload{TadPacketUncompressed};
            payload.insert(payload.end(), 3, 0x19);
            payload.insert(payload.end(), 5, 0x12);

            TadWalkStats stats;
            auto result = tadUnsmartpak(payload, false, false, stats);

            REQUIRE(stats.total() == 0);
            REQUIRE(result.size() == 2);
            REQUIRE(result[0][0] == 0x19);
            REQUIRE(result[1][0] == 0x12);
        }

        SECTION("expands a coalesced tick into a unit state packet")
        {
            TadBytes payload{TadPacketUncompressed};
            payload.insert(payload.end(), {0xfe, 0x40, 0x00, 0x00, 0x00}); // serial 64
            payload.push_back(0xff);
            payload.push_back(0xff);

            TadWalkStats stats;
            auto result = tadUnsmartpak(payload, false, false, stats);

            REQUIRE(stats.total() == 0);
            // The 0xfe itself carries no state of its own, so it is consumed.
            REQUIRE(result.size() == 2);

            REQUIRE(result[0][0] == 0x2c);
            REQUIRE(result[0].size() == 11);
            REQUIRE(result[0][1] == 0x0b); // the declared length agrees
            REQUIRE(result[0][3] == 0x40); // the serial the 0xfe set
            REQUIRE(result[1][3] == 0x41); // and it advances
        }

        SECTION("splices the serial back into a coalesced unit state packet")
        {
            // A 0xfd declaring 12 bytes is 8 on the wire; restoring the four
            // serial bytes brings it back to the 12 its length field claims.
            TadBytes payload{TadPacketUncompressed};
            payload.insert(payload.end(), {0xfe, 0x07, 0x00, 0x00, 0x00});
            payload.insert(payload.end(), {0xfd, 0x0c, 0x00, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5});

            TadWalkStats stats;
            auto result = tadUnsmartpak(payload, false, false, stats);

            REQUIRE(stats.total() == 0);
            REQUIRE(result.size() == 1);
            REQUIRE(result[0][0] == 0x2c);
            REQUIRE(result[0].size() == 12);
            REQUIRE(result[0][1] == 0x0c);
            REQUIRE(result[0][3] == 0x07);
            REQUIRE(result[0][7] == 0xa1);
        }

        SECTION("skips four more bytes on a version 3 payload")
        {
            TadBytes payload{TadPacketUncompressed, 0, 0, 0, 0};
            payload.insert(payload.end(), 3, 0x19);

            TadWalkStats stats;
            auto result = tadUnsmartpak(payload, true, false, stats);

            REQUIRE(stats.total() == 0);
            REQUIRE(result.size() == 1);
            REQUIRE(result[0][0] == 0x19);
        }

        SECTION("decompresses a compressed payload before walking it")
        {
            // A demo packet is compressed with a one-byte header, not the three
            // bytes a wire packet carries, so this is hand-built: three literal
            // 0x19s and a 0x12, then a back-reference producing four more 0x12s,
            // then a zero offset to terminate.
            TadBytes compressed{
                TadPacketCompressed,
                0b00110000, // slots 0-3 literal, slot 4 a match, slot 5 the end
                0x19,
                0x19,
                0x19,
                0x12,
                0x42,
                0x00, // copy 4 bytes from output position 5
                0x00,
                0x00};

            TadWalkStats stats;
            auto result = tadUnsmartpak(compressed, false, false, stats);

            REQUIRE(stats.total() == 0);
            REQUIRE(result.size() == 2);
            REQUIRE(result[0] == TadBytes(3, 0x19));
            REQUIRE(result[1] == TadBytes(5, 0x12));
        }
    }

    TEST_CASE("readTad", "[tad]")
    {
        SECTION("reads the header of a real recording")
        {
            auto handler = readDemo(makeDemo());

            REQUIRE(handler.header.has_value());
            REQUIRE(handler.header->version == 5);
            REQUIRE(handler.header->numPlayers == 2);
            REQUIRE(handler.header->maxUnits == 1000);
            REQUIRE(handler.header->mapName == "[100] Red Hills Mining Town");
        }

        SECTION("reads the extra sectors of a real recording")
        {
            auto handler = readDemo(makeDemo());

            REQUIRE(handler.extraSectors.size() == 4);

            REQUIRE(handler.extraSectors[0].type == static_cast<uint32_t>(TadExtraSectorType::RecorderVersion));
            REQUIRE(stringOf(handler.extraSectors[0].data) == "taf-2026.8.15");

            REQUIRE(handler.extraSectors[1].type == static_cast<uint32_t>(TadExtraSectorType::Date));
            REQUIRE(stringOf(handler.extraSectors[1].data) == "2026-09-07");

            REQUIRE(handler.extraSectors[2].type == static_cast<uint32_t>(TadExtraSectorType::ModId));
            REQUIRE(stringOf(handler.extraSectors[2].data) == "Balanced Annihilation");
        }

        SECTION("de-obfuscates the player address sector")
        {
            auto handler = readDemo(makeDemo());
            REQUIRE(handler.extraSectors[3].type == static_cast<uint32_t>(TadExtraSectorType::PlayerAddr));
            REQUIRE(stringOf(handler.extraSectors[3].data) == "hi");
        }

        SECTION("reads the player table, watchers included")
        {
            auto handler = readDemo(makeDemo());

            REQUIRE(handler.players.size() == 2);
            REQUIRE(handler.players[0].name == "PanchoU");
            REQUIRE(handler.players[0].color == 1);
            REQUIRE(handler.players[0].number == 2);
            REQUIRE_FALSE(handler.players[0].isWatcher());

            REQUIRE(handler.players[1].name == "Adwel91");
            REQUIRE(handler.players[1].isWatcher());
        }

        SECTION("decodes the DirectPlay id out of a player status message")
        {
            auto handler = readDemo(makeDemo());

            REQUIRE(handler.statuses.size() == 2);
            REQUIRE(handler.statuses[0].number == 2);
            REQUIRE(handler.statuses[0].checksum.matches());
            REQUIRE(handler.statuses[0].dplayId == 0x11223344);
            REQUIRE(handler.statuses[1].dplayId == 0x55667788);
        }

        SECTION("reads packets through to a clean end of file")
        {
            TadBytes subPackets;
            subPackets.insert(subPackets.end(), 3, 0x19);
            subPackets.insert(subPackets.end(), 9, 0x0b);

            auto handler = readDemo(makeDemo({
                makePacketRecord(100, 2, subPackets),
                makePacketRecord(250, 3, subPackets),
            }));

            REQUIRE(handler.unitData.size() == 1);
            REQUIRE(handler.packets.size() == 2);
            REQUIRE(handler.packets[0].time == 100);
            REQUIRE(handler.packets[0].sender == 2);
            REQUIRE(handler.packets[1].time == 250);
            REQUIRE(handler.subPackets[0].size() == 2);
            REQUIRE(handler.stats.total() == 0);
        }

        SECTION("rejects a file that is not a demo")
        {
            TadBytes notADemo;
            appendRecord(notADemo, bytesOf("not a demo at all"));
            REQUIRE_THROWS_AS(readDemo(notADemo), TadException);
        }

        SECTION("rejects an unrealistic record length")
        {
            for (auto length : {uint8_t{0}, uint8_t{2}})
            {
                TadBytes bad{length, 0x00, 0x00, 0x00};
                REQUIRE_THROWS_AS(readDemo(bad), TadException);
            }

            TadBytes tooLong{0x01, 0xff, 0x00}; // 0xff01, past the 32768 bound
            REQUIRE_THROWS_AS(readDemo(tooLong), TadException);
        }

        SECTION("rejects a record truncated part-way through")
        {
            auto demo = makeDemo();
            demo.resize(demo.size() - 4);
            REQUIRE_THROWS_AS(readDemo(demo), TadException);
        }

        SECTION("rejects a file that ends before the records the header promises")
        {
            REQUIRE_THROWS_AS(readDemo(realPrefix), TadException);
        }
    }
}
