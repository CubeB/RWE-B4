#include <catch2/catch_test_macros.hpp>
#include <rwe/io/tad/tad_events.h>

// Every byte literal below is a real subpacket lifted out of a real recording,
// identified by demo number and the tick its packet carried, so that a claim
// here can be checked against the file rather than against the last person to
// read it. The demos themselves are not checked in -- tools/fetch-demos.py
// fetches them -- and these tests read no files, like the rest of rwe_test.

namespace rwe
{
    TEST_CASE("tadOwnerBlockOfUnitId", "[tad]")
    {
        SECTION("partitions ids into blocks of maxUnits")
        {
            REQUIRE(tadOwnerBlockOfUnitId(1, 1000) == 0);
            REQUIRE(tadOwnerBlockOfUnitId(1000, 1000) == 0);
            REQUIRE(tadOwnerBlockOfUnitId(1001, 1000) == 1);
            REQUIRE(tadOwnerBlockOfUnitId(2000, 1000) == 1);
            REQUIRE(tadOwnerBlockOfUnitId(2001, 1000) == 2);
        }

        SECTION("reads maxUnits rather than assuming 1000")
        {
            // Demo 14724 is a 1500-unit game: its second player's units start
            // at 1501, where a hardcoded 1000 would put them in block 1 for a
            // while and then in block 2.
            REQUIRE(tadOwnerBlockOfUnitId(1500, 1500) == 0);
            REQUIRE(tadOwnerBlockOfUnitId(1501, 1500) == 1);
            REQUIRE(tadOwnerBlockOfUnitId(2050, 1500) == 1);
        }

        SECTION("has no answer for id zero")
        {
            REQUIRE(!tadOwnerBlockOfUnitId(0, 1000));
            REQUIRE(!tadOwnerBlockOfUnitId(1, 0));
        }
    }

    TEST_CASE("tadDecodeBuildStarted", "[tad]")
    {
        SECTION("a building placed on flat ground")
        {
            // 14724, tick 0: the first thing either commander does.
            // clang-format off
            TadBytes s{
                0x09, 0x29, 0x00, 0x01, 0x00, 0x00, 0x00, 0x43, 0x08, 0x00, 0x00,
                0x03, 0x00, 0x00, 0x00, 0xc4, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00};
            // clang-format on

            auto e = tadDecodeBuildStarted(s);
            REQUIRE(e);
            REQUIRE(e->typeIndex == 41);
            REQUIRE(e->unitId == 1);
            REQUIRE(e->position == TadPosition{138608640, 196608, 46399488});
            REQUIRE(tadFixedToDouble(e->position.x) == 2115.0);
            REQUIRE(tadFixedToDouble(e->position.y) == 3.0);
            REQUIRE(tadFixedToDouble(e->position.z) == 708.0);

            // Flat ground, unrotated: whole-number coordinates and no rotation
            // at all. This is what made the last six bytes legible.
            REQUIRE(e->rotation == TadRotation{0, 0, 0});
        }

        SECTION("a unit leaving a factory that stands on a slope")
        {
            // 14724, tick 9902. The same factory emits type 109 at this exact
            // position 144 times over the game, which is what proved the second
            // id is the new unit and not the builder.
            // clang-format off
            TadBytes s{
                0x09, 0x6d, 0x00, 0x19, 0x00, 0xcb, 0x67, 0x50, 0x07, 0x00, 0x80,
                0x03, 0x00, 0x1b, 0x02, 0x26, 0x03, 0x50, 0x01, 0x00, 0x00, 0xe5,
                0x01};
            // clang-format on

            auto e = tadDecodeBuildStarted(s);
            REQUIRE(e);
            REQUIRE(e->typeIndex == 109);
            REQUIRE(e->unitId == 25);
            REQUIRE(tadFixedToDouble(e->position.y) == 3.5);

            // Pitch and roll, no yaw: standing on sloped terrain.
            REQUIRE(e->rotation == TadRotation{336, 0, 485});
        }

        SECTION("rejects a subpacket of the wrong code or length")
        {
            REQUIRE(!tadDecodeBuildStarted(TadBytes(23, 0x0b)));
            REQUIRE(!tadDecodeBuildStarted(TadBytes(22, 0x09)));
            REQUIRE(!tadDecodeBuildStarted(TadBytes{}));
        }
    }

    TEST_CASE("tadDecodeBuildFinished", "[tad]")
    {
        // 14724, tick 255: unit 1501 -- the second player's commander, the first
        // id in its owner block -- finishes unit 1502.
        auto e = tadDecodeBuildFinished(TadBytes{0x12, 0xde, 0x05, 0xdd, 0x05});
        REQUIRE(e);
        REQUIRE(e->unitId == 1502);
        REQUIRE(e->builderId == 1501);
        REQUIRE(tadOwnerBlockOfUnitId(e->builderId, 1500) == 1);

        REQUIRE(!tadDecodeBuildFinished(TadBytes(5, 0x09)));
    }

    TEST_CASE("tadDecodeDamage", "[tad]")
    {
        // 14724, tick 8084: unit 8 hits unit 1523 for 30. The trailing field
        // reads 322 here and 320, 318, 319, 318, 319 on the five hits that
        // follow, which is why it is not called health.
        auto e = tadDecodeDamage(TadBytes{0x0b, 0xf3, 0x05, 0x08, 0x00, 0x1e, 0x00, 0x42, 0x01});
        REQUIRE(e);
        REQUIRE(e->victimId == 1523);
        REQUIRE(e->attackerId == 8);
        REQUIRE(e->damage == 30);
        REQUIRE(e->unknown == 322);

        REQUIRE(!tadDecodeDamage(TadBytes(9, 0x0c)));
    }

    TEST_CASE("tadDecodeDeath", "[tad]")
    {
        SECTION("a death attributed to a player by DirectPlay id")
        {
            // 14724, tick 5641.
            TadBytes s{0x0c, 0xec, 0x05, 0x43, 0xbc, 0xf8, 0x39, 0xe3, 0x05, 0x00, 0x90};

            auto e = tadDecodeDeath(s);
            REQUIRE(e);
            REQUIRE(e->unitId == 1516);
            REQUIRE(e->killerDplayId == 0x39f8bc43);
            REQUIRE(e->killerId == 1507);
            REQUIRE(e->severity == 0);
            REQUIRE(e->causeAndLevel == 0x90);

            // Cause 9 is the nanoframe decaying out (docs/TOTALA-EXE.md section
            // 92), which skips the death script -- hence severity 0 and nothing
            // left behind. Over the corpus all 949 deaths of this cause read
            // corpse level 0, without exception.
            REQUIRE(e->cause() == 9);
            REQUIRE(e->corpseLevel() == 0);

            // The victim and its killer are in different owner blocks, which is
            // the cheap test for "this was not a self-destruct".
            REQUIRE(tadOwnerBlockOfUnitId(e->unitId, 1500) == 1);
            REQUIRE(tadOwnerBlockOfUnitId(e->killerId, 1500) == 1);
        }

        SECTION("rejects a subpacket of the wrong length")
        {
            REQUIRE(!tadDecodeDeath(TadBytes(10, 0x0c)));
        }
    }

    TEST_CASE("tadDecodeShot", "[tad]")
    {
        // 14733, tick 9916.
        // clang-format off
        TadBytes s{
            0x0d, 0x1d, 0xb4, 0x8c, 0x31, 0x72, 0x3f, 0xa1, 0x00, 0x01, 0x9d,
            0x7b, 0x03, 0x33, 0x5d, 0xf0, 0x31, 0xd8, 0x93, 0x6c, 0x00, 0xcc,
            0x5a, 0xef, 0x04, 0x49, 0x00, 0x97, 0x89, 0xda, 0xfb, 0xf1, 0x03,
            0xe6, 0x07, 0x00};
        // clang-format on

        auto e = tadDecodeShot(s);
        REQUIRE(e);
        REQUIRE(e->shooterId == 2022);
        REQUIRE(e->targetId == 1009);
        REQUIRE(e->unknown == 0);

        // The second triple is far from the first, so it is where the shot was
        // aimed and not a velocity: nothing in TA travels 380 world units in a
        // tick.
        REQUIRE(tadFixedToDouble(e->origin.x) > 12684.0);
        REQUIRE(tadFixedToDouble(e->origin.x) < 12685.0);
        REQUIRE(tadFixedToDouble(e->target.z) > 1263.0);
        REQUIRE(tadFixedToDouble(e->target.z) < 1264.0);

        // Shooter and target belong to different players.
        REQUIRE(tadOwnerBlockOfUnitId(e->shooterId, 1000) == 2);
        REQUIRE(tadOwnerBlockOfUnitId(e->targetId, 1000) == 1);

        REQUIRE(!tadDecodeShot(TadBytes(35, 0x0d)));
    }

    TEST_CASE("tadDecodeScriptCall", "[tad]")
    {
        // 14733, tick 22.
        // clang-format off
        TadBytes s{
            0x10, 0xd1, 0x07, 0x18, 0x00, 0x01, 0x6e, 0xe2, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        // clang-format on

        auto e = tadDecodeScriptCall(s);
        REQUIRE(e);
        REQUIRE(e->unitId == 2001);
        REQUIRE(e->scriptIndex == 24);
        REQUIRE(e->argCount == 1);
        REQUIRE(e->args[0] == 57966);
        REQUIRE(e->args[1] == 0);
        REQUIRE(e->args[2] == 0);
        REQUIRE(e->args[3] == 0);

        REQUIRE(!tadDecodeScriptCall(TadBytes(21, 0x10)));
    }

    TEST_CASE("tadDecodeResourceStats", "[tad]")
    {
        SECTION("a player at the start of a game")
        {
            // 14724, tick 94: both pools full at the default 1000 storage.
            // clang-format off
            TadBytes s{
                0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7a, 0x44,
                0x00, 0x00, 0x7a, 0x44, 0x00, 0x00, 0x7a, 0x44, 0x00, 0x00, 0x7a,
                0x44, 0x00, 0x00, 0xc8, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x96, 0x42, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x40, 0x40};
            // clang-format on

            auto e = tadDecodeResourceStats(s);
            REQUIRE(e);
            REQUIRE(e->metalStored == 1000.0f);
            REQUIRE(e->energyStored == 1000.0f);
            REQUIRE(e->metalStorage == 1000.0f);
            REQUIRE(e->energyStorage == 1000.0f);
            REQUIRE(e->energyCounters[0] == 100.0f);
            REQUIRE(e->energyCounters[2] == 75.0f);
            REQUIRE(e->metalCounters[0] == 4.0f);
            REQUIRE(e->metalCounters[2] == 3.0f);
        }

        SECTION("a watcher's record is the control, and is not a player's")
        {
            // 14733, tick 90, sender 2 -- Roeshambo, a WATCH side. Every field
            // zero except the last of each counter triple, both exactly 1000.
            // No playing peer ever reads like this, so a watcher's record is
            // uninitialised and must be filtered out rather than averaged in.
            // clang-format off
            TadBytes s{
                0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x7a, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x7a, 0x44};
            // clang-format on

            auto e = tadDecodeResourceStats(s);
            REQUIRE(e);
            REQUIRE(e->metalStored == 0.0f);
            REQUIRE(e->metalStorage == 0.0f);
            REQUIRE(e->energyStorage == 0.0f);
            REQUIRE(e->energyCounters[2] == 1000.0f);
            REQUIRE(e->metalCounters[2] == 1000.0f);
        }

        SECTION("rejects a subpacket of the wrong length")
        {
            REQUIRE(!tadDecodeResourceStats(TadBytes(57, 0x28)));
        }
    }

    TEST_CASE("tadDecodeUnitTable", "[tad]")
    {
        SECTION("splits the two blocks and keeps them in order")
        {
            // Entries 0, 1, 317 and 479 of demo 14724's 0x1a record, verbatim.
            // The real record is 317 of each block.
            // clang-format off
            TadBytes record{
                0x1a, 0x02, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xca, 0xba, 0x00, 0x18, 0x34, 0x9e, 0x32,
                0x1a, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0xb9, 0x87, 0x01, 0x42, 0xda, 0xe4, 0x16,
                0x1a, 0x03, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xca, 0xba, 0x00, 0x01, 0x01, 0xff, 0xff,
                0x1a, 0x03, 0x00, 0x00, 0x00, 0x00, 0x57, 0x93, 0x54, 0x92, 0x01, 0x00, 0xff, 0xff};
            // clang-format on

            auto table = tadDecodeUnitTable(record);
            REQUIRE(table);
            REQUIRE(table->listed.size() == 2);
            REQUIRE(table->restricted.size() == 2);
            REQUIRE(table->listed[0].id == 0x00bacab0);
            REQUIRE(table->listed[1].id == 0x0187b903);

            // Ascending by id, which is what makes the two blocks comparable.
            REQUIRE(table->listed[0].id < table->listed[1].id);

            // The restricted block's second field is three packed fields and not
            // a checksum: low byte 1, flag byte 1, and the 0xffff sentinel.
            REQUIRE(table->restricted[0].value == 0xffff0101);

            // ... except on the one entry that is not a unit type, which every
            // demo of both data sets carries and which reads 0 in the flag byte.
            REQUIRE(table->restricted[1].id == TadUnitTable::pseudoEntryId);
            REQUIRE(table->restricted[1].value == 0xffff0001);
        }

        SECTION("rejects a record that is not a whole number of entries")
        {
            REQUIRE(!tadDecodeUnitTable(TadBytes(13, 0x1a)));
            REQUIRE(!tadDecodeUnitTable(TadBytes{}));
        }

        SECTION("rejects a record carrying some other code")
        {
            REQUIRE(!tadDecodeUnitTable(TadBytes(14, 0x09)));
        }
    }

    TEST_CASE("tadDecodeSpeed", "[tad]")
    {
        // 14724, tick 0.
        auto e = tadDecodeSpeed(TadBytes{0x19, 0x00, 0x01});
        REQUIRE(e);
        REQUIRE(e->value == 256);

        REQUIRE(!tadDecodeSpeed(TadBytes(3, 0x1a)));
    }

    TEST_CASE("tadUnitLoadOrder", "[tad]")
    {
        SECTION("sorts, and numbers from one")
        {
            auto order = tadUnitLoadOrder({"CORCV", "ARMCK", "ARMAAP"});
            REQUIRE(order == std::vector<std::string>{"ARMAAP", "ARMCK", "CORCV"});

            REQUIRE(tadUnitNameForTypeIndex(order, 1) == "ARMAAP");
            REQUIRE(tadUnitNameForTypeIndex(order, 3) == "CORCV");

            // Zero is not a type index. It appears nowhere in the corpus, which
            // is the observation the 1-based numbering rests on.
            REQUIRE(!tadUnitNameForTypeIndex(order, 0));
            REQUIRE(!tadUnitNameForTypeIndex(order, 4));
        }

        SECTION("upper-cases before comparing")
        {
            REQUIRE(tadUnitLoadOrder({"corcv", "ARMCK"})
                == std::vector<std::string>{"ARMCK", "CORCV"});
        }

        SECTION("orders by byte, so '_' sorts after 'Z'")
        {
            // TA: Escalation ships ALL_L2.FBI. If '_' (0x5f) were folded in
            // with the letters the whole tail of the order would shift.
            REQUIRE(tadUnitLoadOrder({"ALL_L2", "ALLZZ"})
                == std::vector<std::string>{"ALLZZ", "ALL_L2"});
        }

        SECTION("reproduces the relative order demo 14724 observes")
        {
            // Nine ProTA 4.8 unit types whose identity in demo 14724 is pinned
            // by evidence the ordering was not fitted to -- the side its
            // owner's header entry declares, and the WorkerTime of the builder
            // that built it -- together with the indices the demo carries for
            // them. Sorting has to put them in exactly this sequence, because
            // the indices ascend: 1, 39, 87, 152, 195, 235, 259, 310, 317.
            //
            // ARMAAP and ZZZ are the first and last of the 317 names, which is
            // what fixes the two ends of the order.
            auto order = tadUnitLoadOrder(
                {"CORWIN", "ARMMEX", "ZZZ", "CORRL", "ARMAAP", "CORCV", "ARMWIN", "CORMEX", "ARMCK"});

            REQUIRE(order
                == std::vector<std::string>{
                    "ARMAAP", "ARMCK", "ARMMEX", "ARMWIN", "CORCV", "CORMEX", "CORRL", "CORWIN", "ZZZ"});
        }
    }
}
