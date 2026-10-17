#include <algorithm>
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
    TEST_CASE("a contact that is not visible reads Unidentified object with its sensor prefix (BT-141)", "[presentation]")
    {
        REQUIRE(unidentifiedObjectText(false) == "R: Unidentified object");
        REQUIRE(unidentifiedObjectText(true) == "S: Unidentified object");
    }

    TEST_CASE("vent steam is drawn wherever the ground is explored and dimmed where it is not in sight (BT-151)", "[presentation]")
    {
        REQUIRE(ventSteamDrawMode(true, true) == VentSteamDrawMode::Normal);
        REQUIRE(ventSteamDrawMode(true, false) == VentSteamDrawMode::Dimmed);
        REQUIRE(ventSteamDrawMode(false, false) == VentSteamDrawMode::Hidden);
    }

    TEST_CASE("the target ring is an ellipse, R across and 0.89 R up and down (BT-185)", "[presentation]")
    {
        auto segments = targetRingSegments(Vector2f(100.0f, 50.0f), 32);

        // Sixteen segments of two points each.
        REQUIRE(segments.size() == 32);

        float minX = 1e9f;
        float maxX = -1e9f;
        float minY = 1e9f;
        float maxY = -1e9f;
        for (const auto& p : segments)
        {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        // Horizontal radius 32, vertical (int)(32 x 0.89) = 28.
        REQUIRE(maxX == Catch::Approx(132.0f));
        REQUIRE(minX == Catch::Approx(68.0f));
        REQUIRE(maxY == Catch::Approx(78.0f));
        REQUIRE(minY == Catch::Approx(22.0f));

        // The segments run on from one another and close.
        for (std::size_t i = 1; i < segments.size() - 1; i += 2)
        {
            REQUIRE(segments[i].x == Catch::Approx(segments[i + 1].x));
            REQUIRE(segments[i].y == Catch::Approx(segments[i + 1].y));
        }
        REQUIRE(segments.back().x == Catch::Approx(segments.front().x));
        REQUIRE(segments.back().y == Catch::Approx(segments.front().y));
    }

    TEST_CASE("the target ring's radius is the target's footprint radius, or 32 with no target unit (BT-185)", "[presentation]")
    {
        REQUIRE(targetRingRadius(std::nullopt) == 32);
        REQUIRE(targetRingRadius(std::pair<unsigned int, unsigned int>{2, 2}) == 16);
        REQUIRE(targetRingRadius(std::pair<unsigned int, unsigned int>{3, 5}) == 40);
    }

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
