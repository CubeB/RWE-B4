#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_headers.h>

// The fixtures are the real subpackets tad_events.test.cpp pins its decoders
// to, put through the other direction here: decode what a recording carried,
// encode it again, and require the recording's bytes back. The property
// helpers then cover the whole of what a fixed-length code can carry, which a
// struct-to-struct comparison would not -- a field the encoder dropped would
// leave its bytes behind unchanged, and only a byte comparison sees that.
//
// No files are read, as in the rest of rwe_test.

namespace rwe
{
    namespace
    {
        /**
         * For a fixed-size code the decoder accepts every subpacket with the
         * right first byte and length, so the property is over all of them.
         */
        template <typename Decode, typename Encode>
        void propFixedSubPacketRoundTrip(
            const std::string& description,
            TadSubPacketCode code,
            std::size_t size,
            Decode decode,
            Encode encode)
        {
            rc::prop(description, [code, size, decode, encode]() {
                auto subPacket = *rc::gen::container<TadBytes>(size, rc::gen::arbitrary<uint8_t>());
                subPacket[0] = static_cast<uint8_t>(code);
                RC_ASSERT(tadExpectedSubPacketSize(subPacket.data(), subPacket.size()) == subPacket.size());

                auto decoded = decode(subPacket);
                RC_ASSERT(decoded);
                RC_ASSERT(encode(*decoded) == subPacket);
            });
        }
    }

    TEST_CASE("tadEncodeBuildStarted", "[tad]")
    {
        SECTION("re-encodes a building placed on flat ground")
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
            REQUIRE(tadEncodeBuildStarted(*e) == s);
        }

        SECTION("re-encodes a unit leaving a factory on a slope")
        {
            // 14724, tick 9902: fractional position, pitch and roll, no yaw.
            // clang-format off
            TadBytes s{
                0x09, 0x6d, 0x00, 0x19, 0x00, 0xcb, 0x67, 0x50, 0x07, 0x00, 0x80,
                0x03, 0x00, 0x1b, 0x02, 0x26, 0x03, 0x50, 0x01, 0x00, 0x00, 0xe5,
                0x01};
            // clang-format on

            auto e = tadDecodeBuildStarted(s);
            REQUIRE(e);
            REQUIRE(tadEncodeBuildStarted(*e) == s);
        }

        propFixedSubPacketRoundTrip(
            "re-encoding any 0x09 subpacket reproduces it",
            TadSubPacketCode::UnitBuildStarted,
            23,
            [](const TadBytes& s) { return tadDecodeBuildStarted(s); },
            [](const TadBuildStarted& e) { return tadEncodeBuildStarted(e); });
    }

    TEST_CASE("tadEncodeDamage", "[tad]")
    {
        SECTION("re-encodes a damage record")
        {
            // 14724, tick 8084: unit 8 hits unit 1523 for 30.
            TadBytes s{0x0b, 0xf3, 0x05, 0x08, 0x00, 0x1e, 0x00, 0x42, 0x01};

            auto e = tadDecodeDamage(s);
            REQUIRE(e);
            REQUIRE(tadEncodeDamage(*e) == s);
        }

        propFixedSubPacketRoundTrip(
            "re-encoding any 0x0b subpacket reproduces it",
            TadSubPacketCode::UnitTakeDamage,
            9,
            [](const TadBytes& s) { return tadDecodeDamage(s); },
            [](const TadDamage& e) { return tadEncodeDamage(e); });
    }

    TEST_CASE("tadEncodeDeath", "[tad]")
    {
        SECTION("re-encodes a death with a cause and a corpse level")
        {
            // 14724, tick 5641; cause 9, corpse level 0.
            TadBytes s{0x0c, 0xec, 0x05, 0x43, 0xbc, 0xf8, 0x39, 0xe3, 0x05, 0x00, 0x90};

            auto e = tadDecodeDeath(s);
            REQUIRE(e);
            REQUIRE(tadEncodeDeath(*e) == s);
        }

        propFixedSubPacketRoundTrip(
            "re-encoding any 0x0c subpacket reproduces it",
            TadSubPacketCode::UnitKilled,
            11,
            [](const TadBytes& s) { return tadDecodeDeath(s); },
            [](const TadDeath& e) { return tadEncodeDeath(e); });
    }

    TEST_CASE("tadEncodeShot", "[tad]")
    {
        SECTION("re-encodes a shot")
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
            REQUIRE(tadEncodeShot(*e) == s);
        }

        propFixedSubPacketRoundTrip(
            "re-encoding any 0x0d subpacket reproduces it",
            TadSubPacketCode::WeaponFired,
            36,
            [](const TadBytes& s) { return tadDecodeShot(s); },
            [](const TadShot& e) { return tadEncodeShot(e); });
    }

    TEST_CASE("tadEncodeScriptCall", "[tad]")
    {
        SECTION("re-encodes a script call")
        {
            // 14733, tick 22.
            // clang-format off
            TadBytes s{
                0x10, 0xd1, 0x07, 0x18, 0x00, 0x01, 0x6e, 0xe2, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            // clang-format on

            auto e = tadDecodeScriptCall(s);
            REQUIRE(e);
            REQUIRE(tadEncodeScriptCall(*e) == s);
        }

        propFixedSubPacketRoundTrip(
            "re-encoding any 0x10 subpacket reproduces it",
            TadSubPacketCode::UnitStartScript,
            22,
            [](const TadBytes& s) { return tadDecodeScriptCall(s); },
            [](const TadScriptCall& e) { return tadEncodeScriptCall(e); });
    }

    TEST_CASE("tadEncodeBuildFinished", "[tad]")
    {
        SECTION("re-encodes a build that finished")
        {
            // 14724, tick 255: unit 1501 finishes unit 1502.
            TadBytes s{0x12, 0xde, 0x05, 0xdd, 0x05};

            auto e = tadDecodeBuildFinished(s);
            REQUIRE(e);
            REQUIRE(tadEncodeBuildFinished(*e) == s);
        }

        propFixedSubPacketRoundTrip(
            "re-encoding any 0x12 subpacket reproduces it",
            TadSubPacketCode::UnitBuildFinished,
            5,
            [](const TadBytes& s) { return tadDecodeBuildFinished(s); },
            [](const TadBuildFinished& e) { return tadEncodeBuildFinished(e); });
    }

    TEST_CASE("tadEncodeSpeed", "[tad]")
    {
        SECTION("re-encodes a speed record")
        {
            // 14724, tick 0.
            TadBytes s{0x19, 0x00, 0x01};

            auto e = tadDecodeSpeed(s);
            REQUIRE(e);
            REQUIRE(tadEncodeSpeed(*e) == s);
        }

        propFixedSubPacketRoundTrip(
            "re-encoding any 0x19 subpacket reproduces it",
            TadSubPacketCode::Speed,
            3,
            [](const TadBytes& s) { return tadDecodeSpeed(s); },
            [](const TadSpeed& e) { return tadEncodeSpeed(e); });
    }

    TEST_CASE("tadEncodeResourceStats", "[tad]")
    {
        SECTION("re-encodes a player's opening state")
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
            REQUIRE(tadEncodeResourceStats(*e) == s);
        }

        SECTION("re-encodes a watcher's record")
        {
            // 14733, tick 90, sender 2 -- a WATCH side. Written as it stands
            // even though the fields are uninitialised: an encoder that
            // second-guessed this would move a byte a reader already trusts.
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
            REQUIRE(tadEncodeResourceStats(*e) == s);
        }

        propFixedSubPacketRoundTrip(
            "re-encoding any 0x28 subpacket reproduces it",
            TadSubPacketCode::PlayerResourceInfo,
            58,
            [](const TadBytes& s) { return tadDecodeResourceStats(s); },
            [](const TadResourceStats& e) { return tadEncodeResourceStats(e); });
    }

    TEST_CASE("tadEncodeUnitState", "[tad]")
    {
        SECTION("re-encodes the real subpackets of the corpus")
        {
            // Escalation's layout, as tad_events.test.cpp uses: 549 types, ten
            // bits to an index, CORVENG the one aircraft the fixtures mention.
            std::vector<bool> canFly(549, false);
            canFly[521 - 1] = true;
            auto layout = tadUnitStateLayout(canFly, 1000);

            auto reEncode = [&layout](const TadBytes& subPacket) {
                auto decoded = tadDecodeUnitState(subPacket, layout);
                REQUIRE(decoded);
                REQUIRE(tadEncodeUnitState(*decoded, layout) == subPacket);
            };

            SECTION("a tick with nothing to say")
            {
                // Tick 2; the sync slot 2 is empty.
                reEncode({0x2c, 0x0b, 0x00, 0x02, 0x00, 0x00, 0x00, 0xff, 0xff, 0x01, 0x00});
            }

            SECTION("a ground unit's three waypoints")
            {
                // Tick 182.
                // clang-format off
                reEncode({
                    0x2c, 0x1a, 0x00, 0xb6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x18,
                    0x3a, 0x00, 0xfc, 0x04, 0x3a, 0x00, 0xfa, 0x04, 0x2a, 0x00, 0xea,
                    0xe4, 0xff, 0x3f, 0x00});
                // clang-format on
            }

            SECTION("an aircraft's move goal")
            {
                // Tick 5262.
                // clang-format off
                reEncode({
                    0x2c, 0x1c, 0x00, 0x8e, 0x14, 0x00, 0x00, 0x0c, 0x00, 0x09, 0x06,
                    0x02, 0x00, 0x00, 0x18, 0x02, 0x00, 0xc0, 0x0d, 0x00, 0x00, 0x00,
                    0x71, 0xe1, 0xff, 0x7f, 0x00, 0x00});
                // clang-format on
            }

            SECTION("a finished building's full state with no speed")
            {
                // Tick 1001.
                // clang-format off
                reEncode({
                    0x2c, 0x21, 0x00, 0xe9, 0x03, 0x00, 0x00, 0xff, 0xff, 0x41, 0x51,
                    0x05, 0x00, 0x08, 0x08, 0x00, 0x00, 0x7a, 0x00, 0x00, 0x40, 0x15,
                    0x00, 0x00, 0x00, 0x02, 0x4a, 0xc0, 0x21, 0x00, 0x00, 0x00, 0x00});
                // clang-format on
            }

            SECTION("a nanoframe's build progress")
            {
                // Tick 1005.
                // clang-format off
                reEncode({
                    0x2c, 0x21, 0x00, 0xed, 0x03, 0x00, 0x00, 0xff, 0xff, 0x0d, 0x8a,
                    0x00, 0x48, 0x07, 0x08, 0x00, 0x00, 0x48, 0x00, 0x00, 0x40, 0x15,
                    0x00, 0x00, 0x00, 0xb4, 0x49, 0x1e, 0x1e, 0x00, 0x00, 0x00, 0x00});
                // clang-format on
            }

            SECTION("a mobile unit's full state with a speed")
            {
                // Tick 1000.
                // clang-format off
                reEncode({
                    0x2c, 0x25, 0x00, 0xe8, 0x03, 0x00, 0x00, 0xff, 0xff, 0x6d, 0x20,
                    0x99, 0x00, 0x18, 0xc8, 0x69, 0xeb, 0xcf, 0x04, 0x00, 0x80, 0x15,
                    0x00, 0x66, 0xa5, 0x4f, 0x0a, 0xd7, 0x37, 0x00, 0x00, 0x00, 0x40,
                    0x66, 0x46, 0x00, 0x00});
                // clang-format on
            }

            SECTION("an attached unit's carrier")
            {
                // Tick 2006.
                // clang-format off
                reEncode({
                    0x2c, 0x12, 0x00, 0xd6, 0x07, 0x00, 0x00, 0xff, 0xff, 0x6b, 0xb0,
                    0x07, 0x30, 0x05, 0xa8, 0xef, 0x22, 0x00});
                // clang-format on
            }
        }

        SECTION("covers the optional fields")
        {
            // A stand-in data set: type 1 is a ground unit and type 2 flies,
            // which is all the layout decides. maxUnits 8 makes the sync slot
            // tick % 8 and small enough to read.
            std::vector<bool> canFly{false, true};
            auto layout = tadUnitStateLayout(canFly, 8);

            auto requireRoundTrip = [&layout](const TadUnitState& state) {
                auto bytes = tadEncodeUnitState(state, layout);
                REQUIRE(!bytes.empty());
                REQUIRE(bytes[0] == static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove));
                REQUIRE(tadExpectedSubPacketSize(bytes.data(), bytes.size()) == bytes.size());

                auto decoded = tadDecodeUnitState(bytes, layout);
                REQUIRE(decoded);
                return *decoded;
            };

            SECTION("a tick with no sync record")
            {
                TadUnitState state;
                state.tick = 4;

                auto decoded = requireRoundTrip(state);
                REQUIRE(decoded.tick == 4);
                REQUIRE(decoded.updates.empty());
                REQUIRE(!decoded.sync);
            }

            SECTION("an empty sync slot carries only its type index")
            {
                TadUnitState state;
                state.tick = 3;
                state.sync = TadUnitSync{};
                state.sync->index = 3;

                auto decoded = requireRoundTrip(state);
                REQUIRE(decoded.sync);
                REQUIRE(decoded.sync->index == 3);
                REQUIRE(decoded.sync->typeIndex == 0);
            }

            SECTION("a ground mover's waypoint count, zero to three")
            {
                for (std::size_t count = 0; count <= 3; ++count)
                {
                    std::vector<TadWaypoint> waypoints;
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        auto step = static_cast<int16_t>(1000 * (i + 1));
                        waypoints.push_back(TadWaypoint{step, static_cast<int16_t>(-step)});
                    }

                    TadUnitState state;
                    state.tick = 5;
                    state.updates.push_back(TadUnitUpdate{5, 1, TadGroundPath{count % 2 == 0, waypoints}});
                    state.sync = TadUnitSync{};
                    state.sync->index = 5;

                    auto decoded = requireRoundTrip(state);
                    REQUIRE(decoded.updates.size() == 1);
                    REQUIRE(decoded.updates[0].index == 5);
                    REQUIRE(decoded.updates[0].typeIndex == 1);

                    auto* path = std::get_if<TadGroundPath>(&decoded.updates[0].mover);
                    REQUIRE(path);
                    REQUIRE(path->blocked == (count % 2 == 0));
                    REQUIRE(path->waypoints == waypoints);
                }
            }

            SECTION("an aircraft with no goal")
            {
                TadUnitState state;
                state.tick = 6;
                state.updates.push_back(TadUnitUpdate{1, 2, TadAirMover{std::monostate{}, 1}});
                state.sync = TadUnitSync{};
                state.sync->index = 6;

                auto decoded = requireRoundTrip(state);
                auto* mover = std::get_if<TadAirMover>(&decoded.updates[0].mover);
                REQUIRE(mover);
                REQUIRE(std::holds_alternative<std::monostate>(mover->goal));
                REQUIRE(mover->movementMode == 1);
            }

            SECTION("an aircraft's move goal, over every flag byte")
            {
                for (unsigned int flags = 0; flags <= 0xff; ++flags)
                {
                    TadMoveGoal goal;
                    goal.flags = static_cast<uint8_t>(flags);
                    if (flags & 0x01)
                    {
                        goal.unknown10 = -1234;
                        goal.attachedUnitId = 40000;
                    }
                    if (flags & 0x10)
                    {
                        goal.tolerance = 33;
                    }
                    if (flags & 0x08)
                    {
                        goal.altitude = -511;
                    }
                    if (flags & 0x40)
                    {
                        goal.headingOffset = 17185;
                    }
                    if (flags & 0x20)
                    {
                        goal.position = TadPosition{65536, -131072, 7};
                    }

                    TadUnitState state;
                    state.tick = 7;
                    state.updates.push_back(TadUnitUpdate{2, 2, TadAirMover{goal, 2}});
                    state.sync = TadUnitSync{};
                    state.sync->index = 7;

                    auto decoded = requireRoundTrip(state);
                    auto* mover = std::get_if<TadAirMover>(&decoded.updates[0].mover);
                    REQUIRE(mover);
                    auto* got = std::get_if<TadMoveGoal>(&mover->goal);
                    REQUIRE(got);
                    REQUIRE(got->flags == goal.flags);
                    REQUIRE(got->unknown10 == goal.unknown10);
                    REQUIRE(got->attachedUnitId == goal.attachedUnitId);
                    REQUIRE(got->tolerance == goal.tolerance);
                    REQUIRE(got->altitude == goal.altitude);
                    REQUIRE(got->headingOffset == goal.headingOffset);
                    REQUIRE(got->position == goal.position);
                }
            }

            SECTION("an aircraft's moving goal, with and without a heading")
            {
                for (int hasHeading = 0; hasHeading < 2; ++hasHeading)
                {
                    TadMovingGoal goal;
                    goal.position = TadPosition{1, 2, 3};
                    goal.velocity = TadPosition{-4, 5, -6};
                    if (hasHeading)
                    {
                        goal.heading = 40000;
                    }

                    TadUnitState state;
                    state.tick = 8;
                    state.updates.push_back(TadUnitUpdate{3, 2, TadAirMover{goal, 2}});
                    state.sync = TadUnitSync{};
                    state.sync->index = 0;

                    auto decoded = requireRoundTrip(state);
                    auto* mover = std::get_if<TadAirMover>(&decoded.updates[0].mover);
                    REQUIRE(mover);
                    auto* got = std::get_if<TadMovingGoal>(&mover->goal);
                    REQUIRE(got);
                    REQUIRE(got->position == goal.position);
                    REQUIRE(got->velocity == goal.velocity);
                    REQUIRE(got->heading == goal.heading);
                }
            }

            SECTION("a full state with a position, rotation and speed")
            {
                TadUnitState state;
                state.tick = 10;
                state.sync = TadUnitSync{};
                state.sync->index = 2;
                state.sync->typeIndex = 1;
                state.sync->health = 4900;
                state.sync->buildProgress = 233;
                state.sync->flags10E = 0x12;
                state.sync->motionState = 3;
                state.sync->position = TadPosition{-1, 2, -3};
                state.sync->rotation = TadRotation{111, 222, 333};
                state.sync->speed = -72089;

                auto decoded = requireRoundTrip(state);
                REQUIRE(decoded.sync);
                REQUIRE(decoded.sync->index == 2);
                REQUIRE(decoded.sync->typeIndex == 1);
                REQUIRE(decoded.sync->health == 4900);
                REQUIRE(decoded.sync->buildProgress == 233);
                REQUIRE(decoded.sync->flags10E == 0x12);
                REQUIRE(decoded.sync->motionState == 3);
                REQUIRE(!decoded.sync->carried);
                REQUIRE(decoded.sync->position == state.sync->position);
                REQUIRE(decoded.sync->rotation == state.sync->rotation);
                REQUIRE(decoded.sync->speed == state.sync->speed);
            }

            SECTION("a full state with no speed")
            {
                TadUnitState state;
                state.tick = 10;
                state.sync = TadUnitSync{};
                state.sync->index = 2;
                state.sync->typeIndex = 1;
                state.sync->health = 170;

                auto decoded = requireRoundTrip(state);
                REQUIRE(decoded.sync);
                REQUIRE(!decoded.sync->speed);
            }

            SECTION("a carried unit names its carrier instead of a position")
            {
                TadUnitState state;
                state.tick = 11;
                state.sync = TadUnitSync{};
                state.sync->index = 3;
                state.sync->typeIndex = 2;
                state.sync->health = 17;
                state.sync->buildProgress = 1;
                state.sync->flags10E = 0x20;
                state.sync->motionState = 2;
                state.sync->carried = TadCarried{32767, -128};

                auto decoded = requireRoundTrip(state);
                REQUIRE(decoded.sync);
                REQUIRE(decoded.sync->carried);
                REQUIRE(decoded.sync->carried->carrierId == 32767);
                REQUIRE(decoded.sync->carried->piece == -128);
            }

            SECTION("updates keep their order and the sync record follows them")
            {
                TadUnitState state;
                state.tick = 12;
                state.updates.push_back(TadUnitUpdate{0, 1, TadGroundPath{false, {}}});
                state.updates.push_back(TadUnitUpdate{1, 2, TadAirMover{std::monostate{}, 2}});
                state.updates.push_back(TadUnitUpdate{2, 1, TadGroundPath{true, {{-5, 6}}}});
                state.sync = TadUnitSync{};
                state.sync->index = 4;
                state.sync->typeIndex = 1;
                state.sync->health = 1;

                auto decoded = requireRoundTrip(state);
                REQUIRE(decoded.updates.size() == 3);
                REQUIRE(decoded.updates[0].index == 0);
                REQUIRE(decoded.updates[1].index == 1);
                REQUIRE(decoded.updates[2].index == 2);
                REQUIRE(std::get_if<TadGroundPath>(&decoded.updates[0].mover));
                REQUIRE(std::get_if<TadAirMover>(&decoded.updates[1].mover));

                auto* path = std::get_if<TadGroundPath>(&decoded.updates[2].mover);
                REQUIRE(path);
                REQUIRE(path->blocked);
                REQUIRE(path->waypoints == std::vector<TadWaypoint>{{-5, 6}});

                REQUIRE(decoded.sync);
                REQUIRE(decoded.sync->index == 4);
                REQUIRE(decoded.sync->typeIndex == 1);
            }

            SECTION("a state the layout cannot describe is not encoded")
            {
                TadUnitState state;
                state.tick = 1;
                state.updates.push_back(TadUnitUpdate{0, 2, TadGroundPath{false, {}}});

                // Type 2 flies, so a ground entry for it is not something a
                // receiver reading by type could decode.
                REQUIRE(tadEncodeUnitState(state, layout).empty());

                state.updates[0] = TadUnitUpdate{0, 3, TadGroundPath{false, {}}};
                REQUIRE(tadEncodeUnitState(state, layout).empty());

                state.updates[0] = TadUnitUpdate{0, 0, TadGroundPath{false, {}}};
                REQUIRE(tadEncodeUnitState(state, layout).empty());

                state.updates.clear();
                state.sync = TadUnitSync{};
                state.sync->typeIndex = 3;
                REQUIRE(tadEncodeUnitState(state, layout).empty());

                REQUIRE(tadEncodeUnitState(state, tadUnitStateLayout(std::vector<bool>{}, 8)).empty());
            }
        }
    }
}
