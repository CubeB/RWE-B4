#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <rwe/io/_3do/_3do.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/gaf/GafArchive.h>
#include <rwe/io/gaf/gaf_headers.h>
#include <rwe/io/gaf/gaf_util.h>
#include <rwe/io/hpi/HpiArchive.h>
#include <rwe/io/hpi/hpi_util.h>
#include <rwe/io/pcx/pcx.h>
#include <rwe/io/smk/SmkDecoder.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/net/ta/TaDirectPlay.h>
#include <rwe/net/ta/TaPacket.h>
#include <rwe/net/ta/TaPinger.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/util.h>
#include <sstream>
#include <string>
#include <vector>

/**
 * Files a player downloads -- archives, maps, models, scripts, sprites and
 * films from community sites -- read by code that used to take their sizes,
 * offsets and structure on trust. Each case here is a file shaped to reach
 * one of those places, and each used to corrupt memory, read out of bounds,
 * allocate without limit, or loop or recurse for ever. Now each is refused,
 * or read safely. Issue #75.
 */
namespace rwe
{
    namespace
    {
        template <typename T>
        void append(std::string& bytes, const T& value)
        {
            bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        std::string pcxHeader(uint16_t width, uint16_t height, uint16_t bytesPerLine)
        {
            PcxHeader h{};
            h.manufacturer = 10;
            h.version = 5;
            h.encoding = 1;
            h.bitsPerPixel = 8;
            h.window = PcxWindow{0, 0, static_cast<uint16_t>(width - 1), static_cast<uint16_t>(height - 1)};
            h.numberOfPlanes = 1;
            h.bytesPerLine = bytesPerLine;
            std::string bytes;
            append(bytes, h);
            return bytes;
        }
    }

    TEST_CASE("a PCX whose last run is longer than the row is read without writing past it", "[malformed]")
    {
        // Two rows of four. The second row's run claims 63 bytes; the image
        // has four left.
        auto bytes = pcxHeader(4, 2, 4);
        bytes += std::string("\x01\x02\x03\x04", 4);
        bytes += std::string("\xFF\x07", 2);
        std::vector<char> data(bytes.begin(), bytes.end());
        PcxDecoder decoder(data.cbegin(), data.cend());
        auto image = decoder.decodeImage();
        REQUIRE(image.size() == 8);
        REQUIRE(image[7] == 7);
    }

    TEST_CASE("a PCX that cannot describe itself is refused", "[malformed]")
    {
        SECTION("too short for a header")
        {
            std::vector<char> data(20, 0);
            REQUIRE_THROWS_AS(PcxDecoder(data.cbegin(), data.cend()), PcxException);
        }

        SECTION("a window whose end is before its start")
        {
            auto bytes = pcxHeader(4, 2, 4);
            auto* h = reinterpret_cast<PcxHeader*>(bytes.data());
            h->window.xMin = 10;
            h->window.xMax = 2;
            std::vector<char> data(bytes.begin(), bytes.end());
            REQUIRE_THROWS_AS(PcxDecoder(data.cbegin(), data.cend()), PcxException);
        }

        SECTION("rows shorter than the image is wide, which would have GL read past the pixels")
        {
            auto bytes = pcxHeader(64, 1, 2);
            bytes += std::string("\x01\x02", 2);
            std::vector<char> data(bytes.begin(), bytes.end());
            PcxDecoder decoder(data.cbegin(), data.cend());
            REQUIRE_THROWS_AS(decoder.decodePalettedImage(), PcxException);
        }

        SECTION("too short for the palette it has to end with")
        {
            auto bytes = pcxHeader(4, 1, 4);
            bytes += std::string("\x01\x02\x03\x04", 4);
            std::vector<char> data(bytes.begin(), bytes.end());
            PcxDecoder decoder(data.cbegin(), data.cend());
            REQUIRE_THROWS_AS(decoder.decodePalette(), PcxException);
        }
    }

    TEST_CASE("a PCX with padded rows is cropped to its width", "[malformed]")
    {
        // Three wide, stored four to a row, as the format pads odd widths.
        auto bytes = pcxHeader(3, 2, 4);
        bytes += std::string("\x01\x02\x03\x00\x04\x05\x06\x00", 8);
        std::vector<char> data(bytes.begin(), bytes.end());
        PcxDecoder decoder(data.cbegin(), data.cend());
        auto image = decoder.decodePalettedImage();
        REQUIRE(image == std::vector<char>{1, 2, 3, 4, 5, 6});
    }

    namespace
    {
        std::string hpiPrefix(uint32_t directorySize, uint32_t start)
        {
            std::string bytes;
            append(bytes, HpiVersion{HpiMagicNumber, HpiVersionNumber});
            append(bytes, HpiHeader{directorySize, 0, start});
            return bytes;
        }
    }

    TEST_CASE("an HPI whose root directory starts past its own end is refused before anything is read", "[malformed]")
    {
        // The read used to fill from start to directorySize, a length that
        // wrapped to four billion here, into a buffer of ten bytes.
        auto bytes = hpiPrefix(10, 100);
        bytes += std::string(200, 'x');
        std::istringstream in(bytes);
        REQUIRE_THROWS_AS(HpiArchive(&in), HpiException);
    }

    TEST_CASE("an HPI directory that contains itself is refused", "[malformed]")
    {
        // Root directory data at 20: one entry, listed at 28. The entry is a
        // directory whose data is the root's own, at 20 again.
        auto bytes = hpiPrefix(39, 20);
        append(bytes, HpiDirectoryData{1, 28});
        append(bytes, HpiDirectoryEntry{37, 20, 1});
        bytes += std::string("a\0", 2);
        REQUIRE(bytes.size() == 39);
        std::istringstream in(bytes);
        REQUIRE_THROWS_AS(HpiArchive(&in), HpiException);
    }

    TEST_CASE("an HPI entry whose name starts past the directory is refused", "[malformed]")
    {
        auto bytes = hpiPrefix(37, 20);
        append(bytes, HpiDirectoryData{1, 28});
        append(bytes, HpiDirectoryEntry{5000, 20, 0});
        std::istringstream in(bytes);
        REQUIRE_THROWS_AS(HpiArchive(&in), HpiException);
    }

    namespace
    {
        _3doObject objectAt(uint32_t sibling, uint32_t child)
        {
            _3doObject o{};
            o.magicNumber = _3doMagicNumber;
            o.selectionPrimitiveOffset = -1;
            o.nameOffset = sizeof(_3doObject);
            o.siblingOffset = sibling;
            o.firstChildOffset = child;
            return o;
        }
    }

    TEST_CASE("a 3DO whose objects point back at themselves is refused", "[malformed]")
    {
        SECTION("its own sibling, which looped for ever")
        {
            std::string bytes;
            append(bytes, objectAt(0, 0));
            bytes += std::string("base\0", 5);
            // Sibling offsets of 0 end the list, so the object is placed at 4
            // and names itself.
            std::string file(4, '\0');
            auto o = objectAt(4, 0);
            o.nameOffset = 4 + sizeof(_3doObject);
            append(file, o);
            file += std::string("base\0", 5);
            std::istringstream in(file);
            REQUIRE_THROWS(parse3doObjects(in, 4));
        }

        SECTION("its own child, which recursed until the stack ran out")
        {
            std::string file(4, '\0');
            auto o = objectAt(0, 4);
            o.nameOffset = 4 + sizeof(_3doObject);
            append(file, o);
            file += std::string("base\0", 5);
            std::istringstream in(file);
            REQUIRE_THROWS(parse3doObjects(in, 4));
        }
    }

    TEST_CASE("a 3DO face that names a vertex the piece does not have is emptied, not read", "[malformed]")
    {
        // One vertex, and one triangle naming vertices 0, 1 and 7.
        std::string file;
        auto o = objectAt(0, 0);
        o.numberOfVertices = 1;
        o.numberOfPrimitives = 1;
        o.verticesOffset = sizeof(_3doObject);
        o.primitivesOffset = o.verticesOffset + sizeof(_3doVertex);
        auto primitiveVerticesOffset = o.primitivesOffset + static_cast<uint32_t>(sizeof(_3doPrimitive));
        o.nameOffset = primitiveVerticesOffset + 6;
        append(file, o);
        append(file, _3doVertex{0, 0, 0});
        _3doPrimitive p{};
        p.numberOfVertices = 3;
        p.verticesOffset = primitiveVerticesOffset;
        p.isColored = 1;
        p.colorIndex = 1;
        append(file, p);
        append(file, uint16_t{0});
        append(file, uint16_t{1});
        append(file, uint16_t{7});
        file += std::string("base\0", 5);

        std::istringstream in(file);
        auto objects = parse3doObjects(in, 0);
        REQUIRE(objects.size() == 1);
        REQUIRE(objects[0].primitives.size() == 1);
        REQUIRE(objects[0].primitives[0].vertices.empty());
    }

    TEST_CASE("a model whose piece names loop does not hang the simulation", "[malformed]")
    {
        // "arm" is the root and "ARM" its own child: the name finds the child,
        // whose parent is the name that finds the child.
        std::vector<UnitPieceDefinition> pieces{
            UnitPieceDefinition{"arm", SimVector(0_ss, 0_ss, 0_ss), std::nullopt},
            UnitPieceDefinition{"ARM", SimVector(0_ss, 1_ss, 0_ss), std::string("arm")}};
        auto model = createUnitModelDefinition(10_ss, std::move(pieces));
        std::vector<UnitMesh> meshes(2);
        REQUIRE_NOTHROW(getPieceTransform("arm", model, meshes));
    }

    TEST_CASE("a TNT with sizes past any map is refused", "[malformed]")
    {
        TntHeader h{};
        h.magicNumber = TntMagicNumber;
        h.width = 0x10000;
        h.height = 0x10000;
        std::string bytes;
        append(bytes, h);
        std::istringstream in(bytes);
        REQUIRE_THROWS_AS(TntArchive(&in), TntException);

        h.width = 64;
        h.height = 64;
        h.numberOfTiles = 0xFFFFFFFF;
        bytes.clear();
        append(bytes, h);
        std::istringstream in2(bytes);
        REQUIRE_THROWS_AS(TntArchive(&in2), TntException);
    }

    TEST_CASE("a TNT minimap whose size wraps is refused", "[malformed]")
    {
        // 65536 * 65536 is 0 in 32 bits: a buffer of nothing, then a scan of
        // it by the full width.
        TntHeader h{};
        h.magicNumber = TntMagicNumber;
        h.width = 64;
        h.height = 64;
        h.minimapOffset = sizeof(TntHeader);
        std::string bytes;
        append(bytes, h);
        append(bytes, uint32_t{0x10000});
        append(bytes, uint32_t{0x10000});
        std::istringstream in(bytes);
        TntArchive tnt(&in);
        REQUIRE_THROWS_AS(tnt.readMinimap(), TntException);
    }

    TEST_CASE("a Smacker film whose frame size wraps is refused", "[malformed]")
    {
        std::vector<char> data(104 + 64, 0);
        std::memcpy(data.data(), "SMK2", 4);
        uint32_t side = 0x10000;
        std::memcpy(data.data() + 4, &side, 4);
        std::memcpy(data.data() + 8, &side, 4);
        REQUIRE_THROWS(SmkDecoder(std::move(data)));
    }

    TEST_CASE("TDF blocks nested past any real file are refused, not recursed into", "[malformed]")
    {
        std::string text;
        for (int i = 0; i < 10000; ++i)
        {
            text += "[a]{";
        }
        REQUIRE_THROWS(parseTdfFromString(text));

        // And a real file's depth still reads.
        REQUIRE_NOTHROW(parseTdfFromString("[a]{[b]{[c]{x=1;}}}"));
    }

    TEST_CASE("a COB script whose counts are past any real script is refused", "[malformed]")
    {
        CobHeader h{};
        h.numberOfScripts = 0xFFFFFFFF;
        std::string bytes;
        append(bytes, h);
        std::istringstream in(bytes);
        REQUIRE_THROWS(parseCob(in));
    }

    TEST_CASE("a GAF that claims four billion entries is refused before reserving them", "[malformed]")
    {
        std::string bytes;
        append(bytes, GafHeader{GafVersionNumber, 0xFFFFFFFF, 0});
        std::istringstream in(bytes);
        REQUIRE_THROWS_AS(GafArchive(&in), GafException);
    }

    namespace
    {
        /** A DirectPlay TCP message: the 20-bit size, the token, then "play" and the command. */
        std::vector<std::uint8_t> dpMessage(std::size_t declaredSize, std::uint16_t command, std::size_t payloadSize)
        {
            std::vector<std::uint8_t> bytes(28 + payloadSize, 0);
            auto word = (TaDirectPlayToken << 20) | (static_cast<std::uint32_t>(declaredSize) & 0x000FFFFFu);
            bytes[0] = static_cast<std::uint8_t>(word & 0xFF);
            bytes[1] = static_cast<std::uint8_t>((word >> 8) & 0xFF);
            bytes[2] = static_cast<std::uint8_t>((word >> 16) & 0xFF);
            bytes[3] = static_cast<std::uint8_t>((word >> 24) & 0xFF);
            bytes[4] = 2;
            bytes[20] = 'p';
            bytes[21] = 'l';
            bytes[22] = 'a';
            bytes[23] = 'y';
            bytes[24] = static_cast<std::uint8_t>(command & 0xFF);
            bytes[25] = static_cast<std::uint8_t>(command >> 8);
            bytes[26] = 0x0E;
            return bytes;
        }
    }

    TEST_CASE("a DirectPlay message that lies about its size is refused, never trusted", "[malformed]")
    {
        SECTION("a size larger than the bytes received")
        {
            auto bytes = dpMessage(4096, 0x0001, 32);
            REQUIRE(taPeekDirectPlayMessageSize(bytes.data(), bytes.size()) == 4096);
            REQUIRE_THROWS_AS(taDecodeDirectPlayMessage(bytes.data(), bytes.size()), TaDirectPlayException);
        }

        SECTION("a size smaller than the header")
        {
            auto bytes = dpMessage(8, 0x0001, 4);
            REQUIRE_THROWS_AS(taDecodeDirectPlayMessage(bytes.data(), bytes.size()), TaDirectPlayException);
        }

        SECTION("too short for its own header")
        {
            std::vector<std::uint8_t> small(10, 0);
            REQUIRE_FALSE(taIsDirectPlayMessage(small.data(), small.size()));
            REQUIRE_THROWS_AS(taDecodeDirectPlayMessage(small.data(), small.size()), TaDirectPlayException);
            REQUIRE(taPeekDirectPlayMessageSize(small.data(), small.size()) == 0);
        }

        SECTION("the wrong token")
        {
            auto bytes = dpMessage(32, 0x0005, 4);
            bytes[3] = 0xFF;
            REQUIRE(taPeekDirectPlayMessageSize(bytes.data(), bytes.size()) == 0);
            REQUIRE_THROWS_AS(taDecodeDirectPlayMessage(bytes.data(), bytes.size()), TaDirectPlayException);
        }
    }

    TEST_CASE("a session description too short for its fields is refused", "[malformed]")
    {
        // A 80-byte structure, truncated to 40.
        std::vector<std::uint8_t> payload(40, 0);
        REQUIRE_THROWS_AS(taDecodeSessionDescription(payload), TaDirectPlayException);

        // The reply names its name at offset 92; an offset that points nowhere
        // must be refused rather than walked off the end.
        std::vector<std::uint8_t> reply(84, 0);
        REQUIRE_THROWS_AS(taDecodeEnumSessionsReply(reply), TaDirectPlayException);
    }

    TEST_CASE("an enum players reply that claims more players than it holds is refused", "[malformed]")
    {
        // PlayerCount 0xFFFFFFFF, with a packed offset inside the body.
        std::vector<std::uint8_t> payload(28 + 80, 0);
        payload[0] = 0xFF;
        payload[1] = 0xFF;
        payload[2] = 0xFF;
        payload[3] = 0xFF;
        // descriptionOffset (from the envelope) 36 -> payload offset 28.
        payload[16] = 36;
        // nameOffset 116 -> payload offset 108, inside the 80-byte body only.
        payload[20] = 116;
        // packedOffset 180 -> payload offset 172.
        payload[8] = 180;
        REQUIRE_THROWS_AS(taDecodeSuperEnumPlayersReply(payload), TaDirectPlayException);
    }

    TEST_CASE("a packed player whose strings and lengths run past the message is refused", "[malformed]")
    {
        // A super enum players reply with one player whose service-provider
        // length says there is more data than the message holds.
        std::vector<std::uint8_t> payload(28 + 80, 0);
        payload[24] = 1; // one player
        payload[16] = 36; // descriptionOffset, from the envelope
        payload[20] = 116; // nameOffset, from the envelope
        payload[8] = 180; // packedOffset, from the envelope

        // The player entry starts at payload offset 172: 20 bytes of fixed
        // fields then a one-byte spDataLength. Set infoMask to say the
        // service-provider data is present and claim more than remains.
        payload.resize(28 + 80 + 21, 0);
        payload[172 + 12] = 0x04; // infoMask: sp data present, one-byte length
        payload[172 + 20] = 0xFF; // spDataLength 255, past the end
        REQUIRE_THROWS_AS(taDecodeSuperEnumPlayersReply(payload), TaDirectPlayException);
    }

    TEST_CASE("a create player or add forward request too short for its addresses is refused", "[malformed]")
    {
        std::vector<std::uint8_t> shortPayload(60, 0);
        REQUIRE_THROWS_AS(taDecodeCreatePlayer(shortPayload), TaDirectPlayException);
        REQUIRE_THROWS_AS(taDecodeAddForwardRequest(shortPayload), TaDirectPlayException);

        std::vector<std::uint8_t> empty;
        REQUIRE_THROWS_AS(taDecodeRequestPlayerId(empty), TaDirectPlayException);
        REQUIRE_THROWS_AS(taDecodeRequestPlayerReply(empty), TaDirectPlayException);
        REQUIRE_THROWS_AS(taDecodeDeletePlayer(empty), TaDirectPlayException);
    }

    TEST_CASE("application data too short for its player ids is refused", "[malformed]")
    {
        std::vector<std::uint8_t> tcp(24, 0);
        REQUIRE_THROWS_AS(taDecodeAppDataTcp(tcp.data(), tcp.size()), TaDirectPlayException);

        std::vector<std::uint8_t> udp(4, 0);
        REQUIRE_THROWS_AS(taDecodeAppDataUdp(udp.data(), udp.size()), TaDirectPlayException);
    }

    TEST_CASE("a TA packet too short for its header is dropped, not read past", "[malformed]")
    {
        for (std::size_t size = 0; size < TaPacketHeaderSize; ++size)
        {
            std::vector<std::uint8_t> bytes(size, 0x03);
            CAPTURE(size);
            REQUIRE_FALSE(taParsePacket(bytes));
        }

        // A 0x04 whose compressed stream stops mid-slot has no plain form to
        // walk, so there is nothing to give back.
        TadBytes stopped{0x04, 0x00, 0x00, 0x01, 0x00};
        REQUIRE_FALSE(taParsePacket(stopped));
    }

    TEST_CASE("a subpacket the length table cannot size ends the walk", "[malformed]")
    {
        // 0x6f is not in the table. The rest of the payload is handed over whole
        // rather than absorbed silently, so a desynchronised walk is visible.
        TaPacket unknown;
        unknown.subpackets.push_back({0x6F, 0x01, 0x02});
        auto parsed = taParsePacket(unknown.build());
        REQUIRE(parsed);
        REQUIRE(parsed->stats.unknownCodes == 1);
        REQUIRE(parsed->packet.subpackets.size() == 1);
        REQUIRE(parsed->packet.subpackets[0] == TadBytes({0x6F, 0x01, 0x02}));
    }

    TEST_CASE("a TA packet with a bad checksum is walked and reported, not trusted", "[malformed]")
    {
        TaPacket packet;
        packet.subpackets.push_back({0x06});
        TadBytes bytes = packet.build();
        REQUIRE(taParsePacket(bytes)->checksumValid);

        // Flip a byte inside the encrypted body. The stored checksum no longer
        // matches, and the packet is worth walking anyway: a bad checksum is not
        // a reason to drop a whole session's traffic.
        bytes[4] ^= 0x40;
        auto parsed = taParsePacket(bytes);
        REQUIRE(parsed);
        REQUIRE_FALSE(parsed->checksumValid);
        REQUIRE(parsed->packet.subpackets.size() == 1);

        // The same, in the header, which is not encrypted.
        TadBytes header = packet.build();
        header[3] ^= 0x01;
        auto badHeader = taParsePacket(header);
        REQUIRE(badHeader);
        REQUIRE_FALSE(badHeader->checksumValid);
    }

    TEST_CASE("a subpacket that declares more than the packet holds ends the walk", "[malformed]")
    {
        // 0x2c carries its own length: claim 0xffff of a five-byte payload.
        TaPacket unitState;
        unitState.subpackets.push_back({0x2C, 0xFF, 0xFF, 0x00, 0x00});
        auto parsed = taParsePacket(unitState.build());
        REQUIRE(parsed);
        REQUIRE(parsed->stats.truncated == 1);
        REQUIRE(parsed->packet.subpackets.size() == 1);
        REQUIRE(parsed->packet.subpackets[0].size() == 5);

        // 0xfb's length is a byte over a three-byte prefix.
        TaPacket recorder;
        recorder.subpackets.push_back({0xFB, 0x7F});
        auto parsedFb = taParsePacket(recorder.build());
        REQUIRE(parsedFb);
        REQUIRE(parsedFb->stats.truncated == 1);
        REQUIRE(parsedFb->packet.subpackets.size() == 1);
        REQUIRE(parsedFb->packet.subpackets[0].size() == 2);
    }

    TEST_CASE("a 0x02 that is not thirteen bytes is not a ping", "[malformed]")
    {
        for (std::size_t size = 0; size < 20; ++size)
        {
            if (size == 13)
            {
                continue;
            }
            TadBytes notAPing(size, 0x02);
            CAPTURE(size);
            REQUIRE_FALSE(TaPinger::parsePing(notAPing));
        }
    }
}
