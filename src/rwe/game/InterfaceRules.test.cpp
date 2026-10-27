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

    TEST_CASE("Ctrl+D on a mixed selection cancels the counting units and arms none (BT-181)")
    {
        auto recipients = selfDestructRecipients({false, true, false, true});
        REQUIRE(recipients == std::vector<std::size_t>{1, 3});
    }

    TEST_CASE("Ctrl+D arms the whole selection when none is counting down (BT-181)")
    {
        REQUIRE(selfDestructRecipients({false, false, false}) == std::vector<std::size_t>{0, 1, 2});
        REQUIRE(selfDestructRecipients({}).empty());
    }

    TEST_CASE("the end-of-game fade steps every second tick and takes twenty ticks (BT-197)")
    {
        REQUIRE(endGameFadeStep(0) == 0);
        REQUIRE(endGameFadeStep(1) == 0);
        REQUIRE(endGameFadeStep(2) == 1);
        REQUIRE(endGameFadeStep(19) == 9);
        REQUIRE(endGameFadeStep(20) == 10);
        REQUIRE(endGameFadeStep(500) == 10);
        REQUIRE(EndGameFadeSteps * EndGameFadeTicksPerStep == 20);
    }

    TEST_CASE("any key but a bare modifier or lock dismisses the end screens (BT-199)")
    {
        REQUIRE(keyDismissesEndScreen(SDLK_RETURN));
        REQUIRE(keyDismissesEndScreen(SDLK_SPACE));
        REQUIRE(keyDismissesEndScreen(SDLK_ESCAPE));
        REQUIRE(keyDismissesEndScreen(SDLK_A));
        REQUIRE_FALSE(keyDismissesEndScreen(SDLK_LSHIFT));
        REQUIRE_FALSE(keyDismissesEndScreen(SDLK_RCTRL));
        REQUIRE_FALSE(keyDismissesEndScreen(SDLK_CAPSLOCK));
    }

    TEST_CASE("an armed Build click goes to every mobile builder and to nothing else (BT-108)")
    {
        REQUIRE(buildClickTakesUnit(true, true));
        // A factory, a warehouse, a scout: not given a site to walk to.
        REQUIRE_FALSE(buildClickTakesUnit(true, false));
        REQUIRE_FALSE(buildClickTakesUnit(false, true));
        REQUIRE_FALSE(buildClickTakesUnit(false, false));
    }

    TEST_CASE("the W, B and Y selection-box filters (TA 3.9.01)")
    {
        // isMobile, isArmed, isBuilder
        const bool tank[] = {true, true, false};
        const bool constructor[] = {true, false, true};
        const bool commander[] = {true, true, true};
        const bool factory[] = {false, false, true};
        const bool tower[] = {false, true, false};
        auto admits = [](const BandboxFilter& f, const bool* u) { return bandboxFilterAdmits(f, u[0], u[1], u[2]); };

        SECTION("nothing held takes everything")
        {
            BandboxFilter none;
            for (auto u : {tank, constructor, commander, factory, tower})
            {
                REQUIRE(admits(none, u));
            }
        }

        SECTION("W takes armed mobile units only")
        {
            BandboxFilter w{true, false, false};
            REQUIRE(admits(w, tank));
            REQUIRE(admits(w, commander));
            REQUIRE_FALSE(admits(w, constructor));
            REQUIRE_FALSE(admits(w, factory));
            REQUIRE_FALSE(admits(w, tower));
        }

        SECTION("B takes mobile builders only")
        {
            BandboxFilter b{false, true, false};
            REQUIRE(admits(b, constructor));
            REQUIRE(admits(b, commander));
            REQUIRE_FALSE(admits(b, tank));
            REQUIRE_FALSE(admits(b, factory));
        }

        SECTION("Y takes factories only")
        {
            BandboxFilter y{false, false, true};
            REQUIRE(admits(y, factory));
            REQUIRE_FALSE(admits(y, constructor));
            REQUIRE_FALSE(admits(y, tower));
        }

        SECTION("two held take the union")
        {
            BandboxFilter wy{true, false, true};
            REQUIRE(admits(wy, tank));
            REQUIRE(admits(wy, factory));
            REQUIRE_FALSE(admits(wy, constructor));
        }
    }

    TEST_CASE("Ctrl+F and Ctrl+B find idle factories and idle builders (TA 3.9.01)")
    {
        REQUIRE(isIdleFactory(false, true, true));
        REQUIRE_FALSE(isIdleFactory(false, true, false));
        REQUIRE_FALSE(isIdleFactory(true, true, true));
        REQUIRE_FALSE(isIdleFactory(false, false, true));

        REQUIRE(isIdleBuilder(true, true, true));
        REQUIRE_FALSE(isIdleBuilder(true, true, false));
        REQUIRE_FALSE(isIdleBuilder(false, true, true));
        REQUIRE_FALSE(isIdleBuilder(true, false, true));
    }

    TEST_CASE("the idle-unit keys walk the candidates in id order and wrap")
    {
        std::vector<UnitId> ids{UnitId(7), UnitId(3), UnitId(5)};
        REQUIRE(nextInCycle(ids, std::nullopt) == UnitId(3));
        REQUIRE(nextInCycle(ids, UnitId(3)) == UnitId(5));
        REQUIRE(nextInCycle(ids, UnitId(5)) == UnitId(7));
        REQUIRE(nextInCycle(ids, UnitId(7)) == UnitId(3));
        // The cursor's unit went busy: carry on from where it stood.
        REQUIRE(nextInCycle(ids, UnitId(4)) == UnitId(5));
        REQUIRE(nextInCycle({}, UnitId(3)) == std::nullopt);
    }
}
