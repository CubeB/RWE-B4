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
}
