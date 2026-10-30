#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/io/weapontdf/WeaponTdf.h>

namespace rwe
{
    TEST_CASE("weapon TDF reloadbar is parsed and carried into the definition", "[weapontdf][communitypatch]")
    {
        TdfBlock tagged;
        tagged.insertOrAssignProperty("reloadTime", "5");
        tagged.insertOrAssignProperty("reloadbar", "1");

        const auto taggedTdf = parseWeaponBlock(tagged);
        CHECK(taggedTdf.reloadBar);
        CHECK(parseWeaponDefinition(taggedTdf).reloadBar);

        TdfBlock untagged;
        untagged.insertOrAssignProperty("reloadTime", "5");

        const auto untaggedTdf = parseWeaponBlock(untagged);
        CHECK_FALSE(untaggedTdf.reloadBar);
        CHECK_FALSE(parseWeaponDefinition(untaggedTdf).reloadBar);
    }
}
