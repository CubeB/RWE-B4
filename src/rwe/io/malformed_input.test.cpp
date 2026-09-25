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
}
