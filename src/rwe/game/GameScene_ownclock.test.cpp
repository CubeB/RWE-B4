#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_ownclock.h>

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
