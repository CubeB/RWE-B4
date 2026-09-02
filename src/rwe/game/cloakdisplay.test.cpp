#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

namespace rwe
{
    TEST_CASE("computeUnitDrawStyle: a unit that is not yours and not in sight is not drawn", "[cloakdisplay]")
    {
        REQUIRE(computeUnitDrawStyle(false, false, false) == UnitDrawStyle::Hidden);
    }

    TEST_CASE("computeUnitDrawStyle: a unit that is not yours and in sight is drawn solid", "[cloakdisplay]")
    {
        REQUIRE(computeUnitDrawStyle(false, false, true) == UnitDrawStyle::Solid);
    }

    TEST_CASE("computeUnitDrawStyle: an enemy's cloaked unit is not drawn however well lit it is", "[cloakdisplay]")
    {
        // 0x465AE8 returns zero on the cloak flag before it ever asks about
        // line of sight, so standing in the open makes no difference.
        REQUIRE(computeUnitDrawStyle(false, true, true) == UnitDrawStyle::Hidden);
        REQUIRE(computeUnitDrawStyle(false, true, false) == UnitDrawStyle::Hidden);
    }

    TEST_CASE("computeUnitDrawStyle: your own unit is always drawn, lit or not", "[cloakdisplay]")
    {
        REQUIRE(computeUnitDrawStyle(true, false, true) == UnitDrawStyle::Solid);
        REQUIRE(computeUnitDrawStyle(true, false, false) == UnitDrawStyle::Solid);
    }

    TEST_CASE("computeUnitDrawStyle: your own cloaked unit is drawn, see-through", "[cloakdisplay]")
    {
        // The blit choice at 0x459779 has no ownership test in it: the unit is
        // already past the predicate that would have hidden someone else's, so
        // what is left is your own, and it goes down through the ALPHA TABLE.
        REQUIRE(computeUnitDrawStyle(true, true, true) == UnitDrawStyle::Cloaked);
        REQUIRE(computeUnitDrawStyle(true, true, false) == UnitDrawStyle::Cloaked);
    }

    TEST_CASE("blendCloakedColor: a cloaked pixel is the midpoint of the unit and the background", "[cloakdisplay]")
    {
        // The table at 0x4BA772 is filled with the nearest palette entry to
        // (pal[src] + pal[dst]) / 2, one channel at a time, so a cloaked unit
        // over a dark background lands halfway between the two.
        auto result = blendCloakedColor(Vector3f(0.8f, 0.6f, 0.4f), Vector3f(0.2f, 0.2f, 0.2f));

        REQUIRE(result.x == Catch::Approx(0.5f));
        REQUIRE(result.y == Catch::Approx(0.4f));
        REQUIRE(result.z == Catch::Approx(0.3f));
    }

    TEST_CASE("blendCloakedColor: neither colour is privileged over the other", "[cloakdisplay]")
    {
        // An average is symmetric, and the original's is too -- alpha[a][b]
        // and alpha[b][a] both resolve the same midpoint. A ratio picked by
        // eye rather than read off the table would not survive this.
        Vector3f unitColor(0.9f, 0.25f, 0.7f);
        Vector3f backgroundColor(0.1f, 0.75f, 0.3f);

        auto forwards = blendCloakedColor(unitColor, backgroundColor);
        auto backwards = blendCloakedColor(backgroundColor, unitColor);

        REQUIRE(forwards.x == Catch::Approx(backwards.x));
        REQUIRE(forwards.y == Catch::Approx(backwards.y));
        REQUIRE(forwards.z == Catch::Approx(backwards.z));
        REQUIRE(forwards.x == Catch::Approx(0.5f));
        REQUIRE(forwards.y == Catch::Approx(0.5f));
        REQUIRE(forwards.z == Catch::Approx(0.5f));
    }

    TEST_CASE("blendCloakedColor: a cloaked unit over black keeps half of itself", "[cloakdisplay]")
    {
        auto result = blendCloakedColor(Vector3f(1.0f, 1.0f, 1.0f), Vector3f(0.0f, 0.0f, 0.0f));

        REQUIRE(result.x == Catch::Approx(0.5f));
        REQUIRE(result.y == Catch::Approx(0.5f));
        REQUIRE(result.z == Catch::Approx(0.5f));
    }
}
