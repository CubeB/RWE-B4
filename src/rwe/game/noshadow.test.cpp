#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

namespace rwe
{
    TEST_CASE("unitCastsShadow: NoShadow takes the shadow away", "[noshadow]")
    {
        // Both of the original's shadow passes open with the same test on bit
        // 25 of the definition's flag word and skip the whole draw when it is
        // set: 0x4592A6 loads def+0x241 and 0x4592AC tests 0x2000000, and
        // 0x4594BA / 0x4594C0 do it again for the second path. Fifteen of the
        // shipped units name the key.
        UnitDefinition d{};
        REQUIRE(unitCastsShadow(d));

        d.noShadow = true;
        REQUIRE_FALSE(unitCastsShadow(d));
    }

    TEST_CASE("featureCastsShadow: a feature on ground below the sea level casts none", "[noshadow]")
    {
        // 0x4592D5-0x4592F1: for the Feature Unit (type index 0) the ground
        // under the feature is compared against the sea level byte and a
        // `jl` skips the shadow. Below casts nothing; at or above casts.
        REQUIRE(featureCastsShadow(30_ss, 20_ss));
        REQUIRE(featureCastsShadow(20_ss, 20_ss));
        REQUIRE_FALSE(featureCastsShadow(19_ss, 20_ss));
        REQUIRE_FALSE(featureCastsShadow(0_ss, 20_ss));
    }
}
