#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_ownclock.h>
#include <utility>

namespace rwe
{
    TEST_CASE("applyPeerGameSpeed maps the original's 1..20 levels onto RWE's steps", "[ownclock]")
    {
        SECTION("normal is the original's 10 and RWE's default step")
        {
            auto clock = applyPeerGameSpeed(GameClock{}, 10);
            REQUIRE(clock.speed.index() == GameSpeed::DefaultIndex);
            REQUIRE(clock.speed.perMille() == 1000);
        }

        SECTION("the fastest and slowest original levels land on RWE's ends")
        {
            REQUIRE(applyPeerGameSpeed(GameClock{}, 20).speed.index() == GameSpeed::MaxIndex);
            REQUIRE(applyPeerGameSpeed(GameClock{}, 1).speed.index() == GameSpeed::MinIndex);
            REQUIRE(applyPeerGameSpeed(GameClock{}, 20).speed.perMille() == 2000);
            REQUIRE(applyPeerGameSpeed(GameClock{}, 1).speed.perMille() == 100);
        }

        SECTION("a level past RWE's range clamps rather than wrapping")
        {
            REQUIRE(applyPeerGameSpeed(GameClock{}, 25).speed.index() == GameSpeed::MaxIndex);
            REQUIRE(applyPeerGameSpeed(GameClock{}, 0).speed.index() == GameSpeed::MinIndex);
            REQUIRE(applyPeerGameSpeed(GameClock{}, -5).speed.index() == GameSpeed::MinIndex);
        }

        SECTION("a speed change does not disturb the pause")
        {
            auto paused = applyPeerPause(GameClock{}, true);
            auto changed = applyPeerGameSpeed(paused, 20);
            REQUIRE(changed.paused);
            REQUIRE(changed.speed.index() == GameSpeed::MaxIndex);
        }
    }

    TEST_CASE("a 0x19 is a subtype and a value, not one 16-bit number (BT-178)", "[ownclock]")
    {
        // The start-up record is `00 01` after the code: subtype 0, value 1.
        SECTION("subtype 0 sets the pause to the value, absolutely")
        {
            auto on = decodePeerSpeedPacket(0x0100);
            REQUIRE(on.kind == PeerSpeedPacket::Kind::Pause);
            REQUIRE(on.paused);

            auto off = decodePeerSpeedPacket(0x0000);
            REQUIRE(off.kind == PeerSpeedPacket::Kind::Pause);
            REQUIRE_FALSE(off.paused);

            // Only the low bit counts.
            REQUIRE_FALSE(decodePeerSpeedPacket(0x0200).paused);
        }

        SECTION("subtype 1 sets the level, clamped to 1..20")
        {
            // `01 0C`: a change to level 12, which as one number is 3073.
            auto packet = decodePeerSpeedPacket(0x0C01);
            REQUIRE(packet.kind == PeerSpeedPacket::Kind::Speed);
            REQUIRE(packet.level == 12);

            REQUIRE(decodePeerSpeedPacket(0x0001).level == 1);
            REQUIRE(decodePeerSpeedPacket(0xFF01).level == 20);
        }

        SECTION("any other subtype is left alone")
        {
            REQUIRE(decodePeerSpeedPacket(0x0102).kind == PeerSpeedPacket::Kind::Ignored);
        }
    }

    TEST_CASE("a TA joiner's start-up pause does not hold the host paused (#610)", "[ownclock]")
    {
        SECTION("a pause before the first simulated tick is held back")
        {
            PeerPauseGate gate;
            REQUIRE_FALSE(peerPauseAllowed(gate, true, false));
            // Even after the game starts, with no unpause yet, it is still held back.
            REQUIRE_FALSE(peerPauseAllowed(gate, true, true));
        }

        SECTION("a pause is obeyed once the game runs and the joiner has unpaused")
        {
            PeerPauseGate gate;
            REQUIRE(peerPauseAllowed(gate, false, false));
            REQUIRE_FALSE(peerPauseAllowed(gate, true, false));
            REQUIRE(peerPauseAllowed(gate, true, true));
            REQUIRE(peerPauseAllowed(gate, false, true));
        }
    }

    TEST_CASE("the speed line reads Normal at level 10 and the signed step otherwise (BT-178)", "[ownclock]")
    {
        REQUIRE(gameSpeedMessage(10) == "Game Speed Normal");
        REQUIRE(gameSpeedMessage(12) == "Game Speed  +2");
        REQUIRE(gameSpeedMessage(20) == "Game Speed  +10");
        REQUIRE(gameSpeedMessage(7) == "Game Speed  -3");
        REQUIRE(gameSpeedMessage(1) == "Game Speed  -9");
        REQUIRE(gameSpeedMessage(99) == "Game Speed  +10");
    }

    TEST_CASE("restart, load and save are refused in a network game of either kind (#658)", "[ownclock]")
    {
        auto alone = gameMenuGate(false, false);
        REQUIRE(alone.restart);
        REQUIRE(alone.load);
        REQUIRE(alone.save);

        for (auto [ownClock, lockstep] : {std::pair{true, false}, std::pair{false, true}})
        {
            auto gate = gameMenuGate(ownClock, lockstep);
            REQUIRE_FALSE(gate.restart);
            REQUIRE_FALSE(gate.load);
            REQUIRE_FALSE(gate.save);
        }
    }

    TEST_CASE("applyPeerPause follows the peer and leaves the speed alone", "[ownclock]")
    {
        SECTION("pause and unpause both apply")
        {
            auto paused = applyPeerPause(GameClock{}, true);
            REQUIRE(paused.paused);

            auto resumed = applyPeerPause(paused, false);
            REQUIRE(!resumed.paused);
        }

        SECTION("pausing does not move the speed")
        {
            auto slow = applyPeerGameSpeed(GameClock{}, 3);
            auto paused = applyPeerPause(slow, true);
            REQUIRE(paused.speed.index() == slow.speed.index());
        }
    }
}
