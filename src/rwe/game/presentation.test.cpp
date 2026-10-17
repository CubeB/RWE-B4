#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

/**
 * Small pure rules of how the game is drawn, each pinned to a byte-tactics
 * ruling (static reading at 97d7ac59, not executed against the original).
 * Nothing here reaches the simulation.
 */
namespace rwe
{
    TEST_CASE("a feature marked nodisplayinfo shows no footer text (BT-147)", "[presentation]")
    {
        FeatureDefinition ordinary{};
        ordinary.noDisplayInfo = false;
        REQUIRE(featureFooterIsShown(ordinary));

        FeatureDefinition iceChunk{};
        iceChunk.noDisplayInfo = true;
        REQUIRE_FALSE(featureFooterIsShown(iceChunk));
    }

    TEST_CASE("the nano spray lands in 4/11 to 7/11 of the target's extent (BT-144)", "[presentation]")
    {
        // The window is the middle three elevenths, centred: from -1.5/11 to
        // +1.5/11 about the middle, three elevenths wide, not the six
        // elevenths RWE used to sample.
        REQUIRE(NanoLandingWindowStart == 4.0f / 11.0f);
        REQUIRE(NanoLandingWindowEnd == 7.0f / 11.0f);
        REQUIRE((NanoLandingWindowEnd - NanoLandingWindowStart) == Catch::Approx(3.0f / 11.0f));
        REQUIRE((NanoLandingWindowStart - 0.5f) == Catch::Approx(-1.5f / 11.0f));
        REQUIRE((NanoLandingWindowEnd - 0.5f) == Catch::Approx(1.5f / 11.0f));
    }
}
