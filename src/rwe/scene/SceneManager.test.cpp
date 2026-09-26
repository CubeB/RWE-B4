#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/scene/SceneManager.h>

namespace rwe
{
    TEST_CASE("frameDensityFor")
    {
        REQUIRE(frameDensityFor(1.0f, 1) == Catch::Approx(1.0f));
        REQUIRE(frameDensityFor(2.0f, 1) == Catch::Approx(2.0f));
        REQUIRE(frameDensityFor(2.0f, 2) == Catch::Approx(1.0f));
        REQUIRE(frameDensityFor(1.0f, 2) == Catch::Approx(0.5f));

        // A zero pixel size would be a division by zero; it clamps to 1.
        REQUIRE(frameDensityFor(2.0f, 0) == Catch::Approx(2.0f));
    }
}
