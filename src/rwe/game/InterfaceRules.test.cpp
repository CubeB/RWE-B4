#include <catch2/catch_test_macros.hpp>
#include <rwe/game/InterfaceRules.h>

namespace rwe
{
    TEST_CASE("an armed Capture click accepts any other player's unit, allied or not (BT-107)")
    {
        // The rule is player identity: the function is not even told who is
        // allied with whom, which is the point.
        REQUIRE(armedCaptureAccepts(true, PlayerId(0), PlayerId(1)));
        REQUIRE(armedCaptureAccepts(true, PlayerId(3), PlayerId(0)));
    }

    TEST_CASE("an armed Capture click refuses the orderer's own unit and a unit that cannot capture (BT-107)")
    {
        REQUIRE_FALSE(armedCaptureAccepts(true, PlayerId(2), PlayerId(2)));
        REQUIRE_FALSE(armedCaptureAccepts(false, PlayerId(0), PlayerId(1)));
    }
}
