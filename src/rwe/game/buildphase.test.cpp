#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

namespace
{
    /** A completion fraction that leaves exactly `remaining` of the 0..255 byte. */
    float percentForRemaining(int remaining)
    {
        if (remaining <= 0)
        {
            return 1.0f;
        }
        if (remaining >= 255)
        {
            return 0.0f;
        }
        // Midpoint of the interval that truncates to `remaining`, so float
        // rounding cannot land one row below at a boundary.
        return 1.0f - (static_cast<float>(remaining) + 0.5f) / 255.0f;
    }

    rwe::BuildPhase phaseAt(int remaining)
    {
        return rwe::computeBuildPhase(percentForRemaining(remaining), 0, 0);
    }
}

TEST_CASE("computeBuildPhase boundaries and low-byte wrap", "[game][build]")
{
    using rwe::BuildFillMode;

    SECTION("255")
    {
        auto p = phaseAt(255);
        REQUIRE(p.ratio == 255.0f);
        REQUIRE(p.aboveMode == BuildFillMode::Erase);
        REQUIRE(p.bandMode == BuildFillMode::ColorA);
        REQUIRE(p.belowMode == BuildFillMode::Erase);
    }
    SECTION("236")
    {
        auto p = phaseAt(236);
        REQUIRE(p.ratio == 12.0f);
        REQUIRE(p.aboveMode == BuildFillMode::Erase);
        REQUIRE(p.bandMode == BuildFillMode::ColorA);
        REQUIRE(p.belowMode == BuildFillMode::Erase);
    }
    SECTION("235")
    {
        auto p = phaseAt(235);
        REQUIRE(p.ratio == 255.0f);
        REQUIRE(p.aboveMode == BuildFillMode::Erase);
        REQUIRE(p.bandMode == BuildFillMode::ColorA);
        REQUIRE(p.belowMode == BuildFillMode::Erase);
    }
    SECTION("201")
    {
        auto p = phaseAt(201);
        REQUIRE(p.ratio == 7.0f);
        REQUIRE(p.aboveMode == BuildFillMode::Erase);
        REQUIRE(p.bandMode == BuildFillMode::ColorA);
        REQUIRE(p.belowMode == BuildFillMode::Erase);
    }
    SECTION("200")
    {
        auto p = phaseAt(200);
        REQUIRE(p.ratio == 0.0f);
        REQUIRE(p.aboveMode == BuildFillMode::Erase);
        REQUIRE(p.bandMode == BuildFillMode::ColorB);
        REQUIRE(p.belowMode == BuildFillMode::ColorA);
    }
    SECTION("116")
    {
        auto p = phaseAt(116);
        REQUIRE(p.ratio == 252.0f);
        REQUIRE(p.aboveMode == BuildFillMode::Erase);
        REQUIRE(p.bandMode == BuildFillMode::ColorB);
        REQUIRE(p.belowMode == BuildFillMode::ColorA);
    }
    SECTION("115")
    {
        auto p = phaseAt(115);
        REQUIRE(p.ratio == 0.0f);
        REQUIRE(p.aboveMode == BuildFillMode::ColorA);
        REQUIRE(p.bandMode == BuildFillMode::ColorB);
        REQUIRE(p.belowMode == BuildFillMode::Texture);
    }
    SECTION("31")
    {
        auto p = phaseAt(31);
        REQUIRE(p.ratio == 252.0f);
        REQUIRE(p.aboveMode == BuildFillMode::ColorA);
        REQUIRE(p.bandMode == BuildFillMode::ColorB);
        REQUIRE(p.belowMode == BuildFillMode::Texture);
    }
    SECTION("30")
    {
        auto p = phaseAt(30);
        REQUIRE(p.ratio == 255.0f);
        REQUIRE(p.aboveMode == BuildFillMode::Texture);
        REQUIRE(p.bandMode == BuildFillMode::ColorA);
        REQUIRE(p.belowMode == BuildFillMode::Texture);
    }
    SECTION("0")
    {
        auto p = phaseAt(0);
        REQUIRE(p.ratio == 0.0f);
        REQUIRE(p.aboveMode == BuildFillMode::Texture);
        REQUIRE(p.bandMode == BuildFillMode::ColorA);
        REQUIRE(p.belowMode == BuildFillMode::Texture);
    }
}
