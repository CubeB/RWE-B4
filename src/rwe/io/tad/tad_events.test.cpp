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
        // Weapon1 of the shooter's own FBI: the byte is a 0-based slot index.
        REQUIRE(e->weaponSlot == 0);

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

    TEST_CASE("tadDecodeUnitState", "[tad]")
    {
        // Every fixture is from 14725, a ten-player TA: Escalation game with
        // maxUnits 1000. The layout is Escalation's: 549 types, so a type index
        // is ten bits wide, and the one aircraft these fixtures use is CORVENG
        // at load-order index 521. The other types named are ARMCOM 54, ARMCK
        // 53, ARMMEX 160 and ARMWIN 262 -- none of which fly, which is all the
        // decoder needs to know about them.
        std::vector<bool> canFly(549, false);
        canFly[521 - 1] = true;
        auto layout = tadUnitStateLayout(canFly, 1000);

        SECTION("the type index is as wide as the type count needs")
        {
            REQUIRE(layout.typeIndexBits == 10);
            REQUIRE(tadUnitStateLayout(std::vector<bool>(317, false), 1500).typeIndexBits == 9);
        }

        SECTION("a tick with nothing to say")
        {
            // Tick 2. The terminator, the sync bit, and a sync record whose
            // type is zero because slot 2 is empty. This is exactly the packet
            // a SmartPak 0xff expands to, which is an independent check that
            // the terminator and the sync bit are where the builder puts them.
            TadBytes s{0x2c, 0x0b, 0x00, 0x02, 0x00, 0x00, 0x00, 0xff, 0xff, 0x01, 0x00};

            auto e = tadDecodeUnitState(s, layout);
            REQUIRE(e);
            REQUIRE(e->tick == 2);
            REQUIRE(e->updates.empty());
            REQUIRE(e->sync);
            REQUIRE(e->sync->index == 2);
            REQUIRE(e->sync->typeIndex == 0);
        }

        SECTION("a ground unit's three waypoints")
        {
            // Tick 182: the commander of block 0, walking.
            // clang-format off
            TadBytes s{
                0x2c, 0x1a, 0x00, 0xb6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x18,
                0x3a, 0x00, 0xfc, 0x04, 0x3a, 0x00, 0xfa, 0x04, 0x2a, 0x00, 0xea,
                0xe4, 0xff, 0x3f, 0x00};
            // clang-format on

            auto e = tadDecodeUnitState(s, layout);
            REQUIRE(e);
            REQUIRE(e->tick == 182);
            REQUIRE(e->updates.size() == 1);
            const auto& u = e->updates[0];
            REQUIRE(u.index == 0);
            REQUIRE(tadUnitIdOfIndex(0, u.index, 1000) == 1);
            REQUIRE(u.typeIndex == 54);
            auto path = std::get_if<TadGroundPath>(&u.mover);
            REQUIRE(path);
            REQUIRE(!path->blocked);
            REQUIRE(path->waypoints == std::vector<TadWaypoint>{{464, 10208}, {464, 10192}, {336, 10064}});
            REQUIRE(e->sync);
            REQUIRE(e->sync->typeIndex == 0);
        }

        SECTION("an aircraft's move goal")
        {
            // Tick 5262: a CORVENG whose goal carries only a position (flag
            // 0x20), flying (movement mode 2). The goal's height is 220, under
            // the 511 clamp 0x44E6C0 applies.
            // clang-format off
            TadBytes s{
                0x2c, 0x1c, 0x00, 0x8e, 0x14, 0x00, 0x00, 0x0c, 0x00, 0x09, 0x06,
                0x02, 0x00, 0x00, 0x18, 0x02, 0x00, 0xc0, 0x0d, 0x00, 0x00, 0x00,
                0x71, 0xe1, 0xff, 0x7f, 0x00, 0x00};
            // clang-format on

            auto e = tadDecodeUnitState(s, layout);
            REQUIRE(e);
            REQUIRE(e->updates.size() == 1);
            REQUIRE(e->updates[0].index == 12);
            REQUIRE(e->updates[0].typeIndex == 521);
            auto air = std::get_if<TadAirMover>(&e->updates[0].mover);
            REQUIRE(air);
            REQUIRE(air->movementMode == 2);
            auto goal = std::get_if<TadMoveGoal>(&air->goal);
            REQUIRE(goal);
            REQUIRE(goal->flags == 0x20);
            REQUIRE(!goal->attachedUnitId);
            REQUIRE(!goal->tolerance);
            REQUIRE(goal->position == TadPosition{562036736, 14417920, 386924544});
            REQUIRE(tadFixedToDouble(goal->position->y) == 220.0);

            SECTION("which serialiser follows is decided by the type, not the wire")
            {
                // The same bytes, with CORVENG taken for a ground unit, do not
                // account for their own length.
                REQUIRE(!tadDecodeUnitState(s, tadUnitStateLayout(std::vector<bool>(549, false), 1000)));
            }
        }

        SECTION("a finished building's full state has no speed")
        {
            // Tick 1001, so slot 1 of the sender's block: an ARMMEX at
            // (488, 85, 10248), which is where that unit's 0x09 put it. Health
            // 170, complete, and a yaw of -30975 -- the 0x09's own
            // rotation, once the wire's y, z, x order is put back.
            // clang-format off
            TadBytes s{
                0x2c, 0x21, 0x00, 0xe9, 0x03, 0x00, 0x00, 0xff, 0xff, 0x41, 0x51,
                0x05, 0x00, 0x08, 0x08, 0x00, 0x00, 0x7a, 0x00, 0x00, 0x40, 0x15,
                0x00, 0x00, 0x00, 0x02, 0x4a, 0xc0, 0x21, 0x00, 0x00, 0x00, 0x00};
            // clang-format on

            auto e = tadDecodeUnitState(s, layout);
            REQUIRE(e);
            REQUIRE(e->updates.empty());
            REQUIRE(e->sync);
            const auto& u = *e->sync;
            REQUIRE(u.index == 1);
            REQUIRE(u.typeIndex == 160);
            REQUIRE(u.health == 170);
            REQUIRE(u.buildProgress == 0);
            REQUIRE(u.motionState == 1);
            REQUIRE(!u.carried);
            REQUIRE(u.position == TadPosition{31981568, 5570560, 671612928});
            REQUIRE(tadFixedToDouble(u.position.x) == 488.0);
            REQUIRE(u.rotation == TadRotation{0, -30975, 0});
            REQUIRE(!u.speed);
        }

        SECTION("a nanoframe carries its build progress")
        {
            // Tick 1005: an ARMWIN nanoframe at 17 health; 233 is
            // 1 + trunc(254 * remaining), so about 9% built.
            // clang-format off
            TadBytes s{
                0x2c, 0x21, 0x00, 0xed, 0x03, 0x00, 0x00, 0xff, 0xff, 0x0d, 0x8a,
                0x00, 0x48, 0x07, 0x08, 0x00, 0x00, 0x48, 0x00, 0x00, 0x40, 0x15,
                0x00, 0x00, 0x00, 0xb4, 0x49, 0x1e, 0x1e, 0x00, 0x00, 0x00, 0x00};
            // clang-format on

            auto e = tadDecodeUnitState(s, layout);
            REQUIRE(e);
            REQUIRE(e->sync->typeIndex == 262);
            REQUIRE(e->sync->health == 17);
            REQUIRE(e->sync->buildProgress == 233);
            REQUIRE(!e->sync->speed);
        }

        SECTION("a mobile unit's full state ends with its speed")
        {
            // Tick 1000, slot 0: the commander, moving at 72089 / 65536 = 1.1
            // world units a tick, which is ARMCOM's MaxVelocity.
            // clang-format off
            TadBytes s{
                0x2c, 0x25, 0x00, 0xe8, 0x03, 0x00, 0x00, 0xff, 0xff, 0x6d, 0x20,
                0x99, 0x00, 0x18, 0xc8, 0x69, 0xeb, 0xcf, 0x04, 0x00, 0x80, 0x15,
                0x00, 0x66, 0xa5, 0x4f, 0x0a, 0xd7, 0x37, 0x00, 0x00, 0x00, 0x40,
                0x66, 0x46, 0x00, 0x00};
            // clang-format on

            auto e = tadDecodeUnitState(s, layout);
            REQUIRE(e);
            REQUIRE(e->sync->index == 0);
            REQUIRE(e->sync->typeIndex == 54);
            REQUIRE(e->sync->health == 4900);
            REQUIRE(e->sync->position == TadPosition{322940327, 5636096, 691967384});
            REQUIRE(e->sync->speed == 72089);
        }

        SECTION("an attached unit carries its carrier in place of a position")
        {
            // Tick 2006: an ARMCK nanoframe, attached to unit 3006.
            // clang-format off
            TadBytes s{
                0x2c, 0x12, 0x00, 0xd6, 0x07, 0x00, 0x00, 0xff, 0xff, 0x6b, 0xb0,
                0x07, 0x30, 0x05, 0xa8, 0xef, 0x22, 0x00};
            // clang-format on

            auto e = tadDecodeUnitState(s, layout);
            REQUIRE(e);
            REQUIRE(e->sync->index == 6);
            REQUIRE(e->sync->typeIndex == 53);
            REQUIRE(e->sync->buildProgress == 166);
            REQUIRE(e->sync->carried);
            REQUIRE(e->sync->carried->carrierId == 3006);
            REQUIRE(e->sync->carried->piece == 1);
        }

        SECTION("refuses what does not account for its own length")
        {
            TadBytes s{0x2c, 0x0b, 0x00, 0x02, 0x00, 0x00, 0x00, 0xff, 0xff, 0x01, 0x00};

            auto shortened = s;
            shortened.pop_back();
            shortened[1] = 0x0a;
            REQUIRE(!tadDecodeUnitState(shortened, layout));

            auto padded = s;
            padded.push_back(0);
            padded[1] = 0x0c;
            REQUIRE(!tadDecodeUnitState(padded, layout));

            auto lying = s;
            lying[1] = 0x0c;
            REQUIRE(!tadDecodeUnitState(lying, layout));
        }
    }
}
