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

    TEST_CASE("a release is a click inside 25 thirtieths of a second and 32 world units (BT-102)")
    {
        // 833 ms is 24 thirtieths (24.99 truncates), 834 ms is 25.
        REQUIRE(pressIsClick(1000, 1000, 0.0f, 0.0f));
        REQUIRE(pressIsClick(0, 833, 0.0f, 0.0f));
        REQUIRE_FALSE(pressIsClick(0, 834, 0.0f, 0.0f));

        // Each axis on its own, strictly under 32 either way.
        REQUIRE(pressIsClick(0, 0, 31.9f, -31.9f));
        REQUIRE_FALSE(pressIsClick(0, 0, 32.0f, 0.0f));
        REQUIRE_FALSE(pressIsClick(0, 0, 0.0f, -32.0f));
    }

    TEST_CASE("the click clock is the wall clock scaled to thirtieths, truncated before the difference (BT-102)")
    {
        // 33 ms and 866 ms read 0 and 25 on the original's clock: one more than
        // the gap of 833 ms between them would suggest.
        REQUIRE_FALSE(pressIsClick(33, 866, 0.0f, 0.0f));
        REQUIRE(pressIsClick(34, 866, 0.0f, 0.0f));
    }

    TEST_CASE("the select-everything sweep takes finished, uncarried units of any kind (BT-180)")
    {
        REQUIRE(sweepSelectsUnit(false, false));
        // A nanoframe and a unit riding in a transport are left out.
        REQUIRE_FALSE(sweepSelectsUnit(true, false));
        REQUIRE_FALSE(sweepSelectsUnit(false, true));
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
