#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <deque>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/_3do/_3do.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/gaf/GafArchive.h>
#include <rwe/io/gaf/gaf_headers.h>
#include <rwe/io/gaf/gaf_util.h>
#include <rwe/io/hpi/HpiArchive.h>
#include <rwe/io/hpi/hpi_util.h>
#include <rwe/io/pcx/pcx.h>
#include <rwe/io/smk/SmkDecoder.h>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/puppet/TaLiveReceiver.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/puppet/puppet_test_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/sim/util.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    TEST_CASE("an FBI with no SoundCategory is silent, not refused", "[malformed]")
    {
        // ProTA's MAKENUKE/MAKEANTI pseudo-units name no category; the
        // original reads the missing key as no sound at all.
        auto fbi = parseUnitFbi(parseTdfFromString("[UNITINFO]\n{\nUnitName=MAKENUKEARM;\nObjectname=;\n}\n"));
        REQUIRE(fbi.unitName == "MAKENUKEARM");
        REQUIRE(fbi.soundCategory.empty());
    }

    TEST_CASE("a referenced feature no TDF defines is dropped, not given an id", "[malformed]")
    {
        std::unordered_map<std::string, FeatureDefinitionId> featureNameIndex;
        std::unordered_set<std::string> knownFeatureNames{"TREE"};
        std::deque<std::string> openQueue;
        std::unordered_map<std::string, FeatureDefinitionId> openSet;
        auto nextId = FeatureDefinitionId(7);

        // A present name is queued and takes the next id.
        auto tree = getFeatureId(nextId, featureNameIndex, knownFeatureNames, openQueue, openSet, "tree");
        REQUIRE(tree);
        REQUIRE(tree->value == 7);
        REQUIRE(nextId.value == 8);
        REQUIRE(openQueue.size() == 1);

        // A missing name yields nothing and leaves the next id alone, so a
        // reference to a corpse no TDF defines cannot point at whichever
        // feature is loaded next.
        REQUIRE_FALSE(getFeatureId(nextId, featureNameIndex, knownFeatureNames, openQueue, openSet, "TREEDEAD"));
        REQUIRE(nextId.value == 8);
        REQUIRE(openQueue.size() == 1);

        // A name already loaded resolves to its id without reserving another.
        featureNameIndex.insert({"ROCK", FeatureDefinitionId(3)});
        auto rock = getFeatureId(nextId, featureNameIndex, knownFeatureNames, openQueue, openSet, "ROCK");
        REQUIRE(rock);
        REQUIRE(rock->value == 3);
        REQUIRE(nextId.value == 8);
    }

    TEST_CASE("a puppet driver drops a demo record it cannot place, and never indexes with it", "[malformed][puppet]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        addWellStockedPlayer(sim, "ARM");
        addWellStockedPlayer(sim, "CORE");

        TadPuppetDriver driver(sim, 8, puppetTestLoadOrder());
        driver.addPlayer(1, PlayerId(0));
        driver.addPlayer(2, PlayerId(1));

        auto feed = [&](uint8_t sender, const std::vector<TadBytes>& subs) {
            driver.onPacket(TadPacket{0, sender}, subs);
        };

        SECTION("damage for an id no puppet holds")
        {
            feed(1, {tadEncodeDamage(TadDamage{3, 0, 5, 0})});
            REQUIRE(driver.stats().recordsDroppedUnknownUnit == 1);
        }

        SECTION("a death whose id is zero, and one in a block no sender claimed")
        {
            feed(1, {tadEncodeDeath(TadDeath{0, 0xffffffffu, 0, 100, 0})});
            REQUIRE(driver.stats().recordsDroppedBadId == 1);

            // Block 5 is nobody's: sender 1 has sent no 0x09 to claim it.
            feed(1, {tadEncodeDeath(TadDeath{41, 0xffffffffu, 0, 100, 0})});
            REQUIRE(driver.stats().recordsDroppedBadBlock == 1);
        }

        SECTION("a build finished for an id no puppet holds")
        {
            feed(1, {tadEncodeBuildFinished(TadBuildFinished{3, 0})});
            REQUIRE(driver.stats().recordsDroppedUnknownUnit == 1);
        }

        SECTION("a build started whose type index is past the load order")
        {
            feed(1, {tadEncodeBuildStarted(TadBuildStarted{99, 1, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}})});
            REQUIRE(driver.stats().recordsDroppedBadType == 1);
            REQUIRE(driver.stats().unitsSpawned == 0);
        }

        SECTION("a full-state record for an empty slot, and a truncated 0x2c")
        {
            std::vector<bool> canFly{false, false, false};
            auto layout = tadUnitStateLayout(canFly, 8);

            TadUnitState empty;
            empty.tick = 1;
            empty.sync = TadUnitSync{0, 0, 0, 0, 0, 0, std::nullopt, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}, std::nullopt};
            feed(1, {tadEncodeUnitState(empty, layout)});
            REQUIRE(driver.stats().unitsSpawned == 0);

            feed(1, {TadBytes{0x2c, 0x00, 0x00}});
            REQUIRE(driver.stats().recordsDroppedBadType == 1);
        }
    }

    TEST_CASE("the puppet driver's newer records are bounded like the rest", "[malformed][puppet]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        addWellStockedPlayer(sim, "ARM");
        addWellStockedPlayer(sim, "CORE");

        TadPuppetDriver driver(sim, 8, puppetTestLoadOrder());
        driver.addPlayer(1, PlayerId(0));
        driver.addPlayer(2, PlayerId(1));

        auto feed = [&](uint8_t sender, const std::vector<TadBytes>& subs) {
            driver.onPacket(TadPacket{0, sender}, subs);
        };

        SECTION("a shot whose shooter no puppet holds is dropped, not indexed with")
        {
            auto shoot = [&](uint16_t shooter, uint8_t slot) {
                return tadEncodeShot(TadShot{
                    TadPosition{0, 0, 0}, TadPosition{65536, 0, 0}, TadRotation{0, 0, 0}, 0, shooter, slot});
            };

            feed(1, {shoot(99, 0)});
            REQUIRE(driver.stats().shotsDropped == 1);

            // A puppet exists but has no weapon in slot 7, so the slot is
            // refused before the weapon map is reached.
            feed(1, {tadEncodeBuildStarted(TadBuildStarted{3, 1, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}})});
            feed(1, {shoot(1, 7)});
            REQUIRE(driver.stats().shotsDropped == 2);
            REQUIRE(driver.stats().shotsSpawned == 0);
        }

        SECTION("a script call for no puppet, and one past its script table")
        {
            feed(1, {tadEncodeScriptCall(TadScriptCall{99, 0, 2, {1, 2, 3, 0}})});
            REQUIRE(driver.stats().scriptCallsDropped == 1);

            feed(1, {tadEncodeBuildStarted(TadBuildStarted{3, 1, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}})});
            feed(1, {tadEncodeScriptCall(TadScriptCall{1, 0xffff, 9, {1, 2, 3, 4}})});
            REQUIRE(driver.stats().scriptCallsDropped == 2);
            REQUIRE(driver.stats().scriptCallsRun == 0);
        }

        SECTION("a chat line from a sender no player stands for is ignored")
        {
            TadBytes chat(65, 0);
            chat[0] = 0x05;
            chat[1] = 'h';
            chat[2] = 'i';

            feed(3, {chat});
            REQUIRE(driver.stats().chatLines == 0);
            REQUIRE(driver.takeChat().empty());
        }

        SECTION("resource statistics for an unmapped sender change nobody")
        {
            TadResourceStats stats{};
            stats.metalStored = 123.0f;
            feed(3, {tadEncodeResourceStats(stats)});
            REQUIRE(sim.getPlayer(PlayerId(0)).metal == Metal(10000.0f));
        }

        SECTION("a truncated speed record changes nothing, a whole one is stored")
        {
            feed(1, {TadBytes{0x19, 0x00}});
            REQUIRE(driver.stats().speedChanges == 0);
            REQUIRE_FALSE(driver.takeSpeedChange());

            feed(1, {tadEncodeSpeed(TadSpeed{256})});
            REQUIRE(driver.stats().speedChanges == 1);
            auto speed = driver.takeSpeedChange();
            REQUIRE(speed);
            REQUIRE(*speed == 256);
            REQUIRE_FALSE(driver.takeSpeedChange());
        }
    }

    TEST_CASE("a live receiver refuses a serial it cannot make sense of", "[malformed][puppet]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        addWellStockedPlayer(sim, "ARM");

        TadPuppetDriver driver(sim, 8, puppetTestLoadOrder());
        driver.addPlayer(1, PlayerId(0));
        driver.setExternalClock(true);
        TaLiveReceiver receiver(driver);

        auto layout = tadUnitStateLayout(std::vector<bool>{false, false, false}, 8);
        auto record = [&](uint32_t serial) {
            return tadEncodeUnitState(
                TadUnitState{
                    serial,
                    {},
                    TadUnitSync{0, 3, 100, 0, 0, 0, std::nullopt, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}, std::nullopt}},
                layout);
        };
        uint32_t sequence = 1000;
        auto feed = [&](const TadBytes& subPacket) {
            receiver.onPacket(TadPacket{0, 1}, {subPacket}, 0, sequence--);
        };

        SECTION("a 0x2c too short to carry the serial, and one that lies about its length")
        {
            REQUIRE_FALSE(tadSerialOfUnitState(TadBytes{0x2c, 0x03, 0x00}));
            REQUIRE_FALSE(tadSerialOfUnitState(TadBytes{0x2c, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff}));
            REQUIRE(tadSerialOfUnitState(record(8)) == 8u);
        }

        SECTION("a subpacket that is not a 0x2c has no serial")
        {
            REQUIRE_FALSE(tadSerialOfUnitState(TadBytes{}));
            REQUIRE_FALSE(tadSerialOfUnitState(TadBytes{0x2d, 0x00, 0x00}));
            REQUIRE_FALSE(tadSerialOfUnitState(TadBytes{0x0b, 0x08, 0x00, 0, 0, 0, 0, 0}));
        }

        SECTION("a peer that names a tick a long way off is dropped, not waited for")
        {
            feed(record(8));
            auto far = record(0xffffffffu);
            for (int i = 0; i < 1000; ++i)
            {
                feed(far);
            }

            REQUIRE(receiver.stats().packetsReceived == 1001);
            REQUIRE(receiver.stats().packetsDroppedOutOfRange == 1000);
            REQUIRE(receiver.stats().held == 1);
            REQUIRE(driver.stats().packets == 0);
        }

        SECTION("once a packet has been applied the clock is fixed, and a serial below it is refused")
        {
            feed(record(8));
            receiver.onTick(0);
            driver.applyTick(0);
            feed(record(0));

            REQUIRE(receiver.stats().clock.originSerial.value_or(0) == 8u);
            REQUIRE(receiver.stats().packetsDroppedOutOfRange == 1);
        }

        SECTION("a flood of in-range serials is bounded, and what it lost is counted")
        {
            for (uint32_t serial = 1; serial <= 2000; ++serial)
            {
                feed(record(serial));
            }

            REQUIRE(receiver.stats().held <= receiver.options().maxHeld);
            REQUIRE(receiver.stats().packetsDroppedBufferFull > 0);
            REQUIRE(receiver.stats().packetsApplied == 0);
        }
    }
}
