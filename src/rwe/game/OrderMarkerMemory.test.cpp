#include <catch2/catch_test_macros.hpp>
#include <rwe/game/OrderMarkerMemory.h>

namespace rwe
{
    TEST_CASE("OrderMarkerMemory freezes a hidden target's x and z (BT-184)")
    {
        OrderMarkerMemory memory;
        UnitId target(7);

        SECTION("a visible target is drawn where it is")
        {
            SimVector live(SimScalar(10), SimScalar(2), SimScalar(20));
            REQUIRE((memory.resolve(target, live, true) == live));
        }

        SECTION("once hidden, x and z stay at the last place it was seen")
        {
            memory.resolve(target, SimVector(SimScalar(10), SimScalar(2), SimScalar(20)), true);
            auto drawn = memory.resolve(target, SimVector(SimScalar(500), SimScalar(2), SimScalar(600)), false);
            REQUIRE(drawn.x == SimScalar(10));
            REQUIRE(drawn.z == SimScalar(20));
        }

        SECTION("the height keeps following the live target")
        {
            memory.resolve(target, SimVector(SimScalar(10), SimScalar(2), SimScalar(20)), true);
            auto drawn = memory.resolve(target, SimVector(SimScalar(500), SimScalar(9), SimScalar(600)), false);
            REQUIRE(drawn.y == SimScalar(9));
        }

        SECTION("seen again, the marker catches up")
        {
            memory.resolve(target, SimVector(SimScalar(10), SimScalar(2), SimScalar(20)), true);
            memory.resolve(target, SimVector(SimScalar(500), SimScalar(2), SimScalar(600)), false);
            auto drawn = memory.resolve(target, SimVector(SimScalar(40), SimScalar(2), SimScalar(50)), true);
            REQUIRE(drawn.x == SimScalar(40));
            REQUIRE(drawn.z == SimScalar(50));
        }

        SECTION("a target never seen is filled from its true position, then frozen")
        {
            auto first = memory.resolve(target, SimVector(SimScalar(5), SimScalar(1), SimScalar(6)), false);
            REQUIRE(first.x == SimScalar(5));
            auto second = memory.resolve(target, SimVector(SimScalar(99), SimScalar(1), SimScalar(98)), false);
            REQUIRE(second.x == SimScalar(5));
            REQUIRE(second.z == SimScalar(6));
        }

        SECTION("targets are remembered separately")
        {
            memory.resolve(target, SimVector(SimScalar(1), SimScalar(0), SimScalar(1)), true);
            memory.resolve(UnitId(8), SimVector(SimScalar(2), SimScalar(0), SimScalar(2)), true);
            auto drawn = memory.resolve(UnitId(8), SimVector(SimScalar(70), SimScalar(0), SimScalar(70)), false);
            REQUIRE(drawn.x == SimScalar(2));
        }
    }
}
