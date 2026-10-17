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

    TEST_CASE("the paging keys close the build menu off either end and open it from closed (BT-104)")
    {
        // Three pages. Forward: 1 -> 2 -> 3 -> closed -> 1.
        auto at = BuildMenuPosition{true, 0};
        at = stepBuildMenu(at, 3, true, true);
        REQUIRE((at.open && at.page == 1));
        at = stepBuildMenu(at, 3, true, true);
        REQUIRE((at.open && at.page == 2));
        at = stepBuildMenu(at, 3, true, true);
        REQUIRE_FALSE(at.open);
        at = stepBuildMenu(at, 3, true, true);
        REQUIRE((at.open && at.page == 0));

        // Backward from closed opens the last page; off the first page closes.
        at = stepBuildMenu(BuildMenuPosition{false, 0}, 3, false, true);
        REQUIRE((at.open && at.page == 2));
        at = stepBuildMenu(BuildMenuPosition{true, 0}, 3, false, true);
        REQUIRE_FALSE(at.open);

        // A single page opens and then closes.
        at = stepBuildMenu(BuildMenuPosition{false, 0}, 1, true, true);
        REQUIRE((at.open && at.page == 0));
        at = stepBuildMenu(at, 1, true, true);
        REQUIRE_FALSE(at.open);
    }

    TEST_CASE("the NEXT and PREV buttons wrap and keep the menu open (BT-104)")
    {
        auto at = stepBuildMenu(BuildMenuPosition{true, 2}, 3, true, false);
        REQUIRE((at.open && at.page == 0));
        at = stepBuildMenu(at, 3, false, false);
        REQUIRE((at.open && at.page == 2));
    }

    TEST_CASE("a unit with no build pages is not paged (BT-104)")
    {
        auto at = stepBuildMenu(BuildMenuPosition{false, 0}, 0, true, true);
        REQUIRE_FALSE(at.open);
    }

    TEST_CASE("an armed Build click goes to every mobile builder and to nothing else (BT-108)")
    {
        REQUIRE(buildClickTakesUnit(true, true));
        // A factory, a warehouse, a scout: not given a site to walk to.
        REQUIRE_FALSE(buildClickTakesUnit(true, false));
        REQUIRE_FALSE(buildClickTakesUnit(false, true));
        REQUIRE_FALSE(buildClickTakesUnit(false, false));
    }
}
