#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <string>

namespace rwe
{
    TEST_CASE("weapon TDF IDs above 255 survive parsing and loading", "[weapontdf]")
    {
        // TADR 2026.10.6, "Weapon IDs above 255": vanilla TA's 256-record
        // weapon table makes ID=256 and above corrupt memory at load, and the
        // patch extends the usable range to 0..4095. RWE has no such table:
        // weapons are keyed by name and the id is carried as an unsigned int
        // (BT-WEAPONS-WEAPON-TYPES-PARITY.md section 1), so there is nothing
        // to narrow or gate. This is a regression test on that property.
        for (const unsigned int id : {0u, 20u, 255u, 256u, 300u, 4095u})
        {
            CAPTURE(id);

            auto tdf = parseTdfFromString(
                "[WEAPON]\n{\nID=" + std::to_string(id) + ";\nrange=100;\n}\n");
            auto weapons = parseWeaponTdf(tdf);
            REQUIRE(weapons.size() == 1);
            CHECK(weapons[0].second.id == id);

            // The loader's conversion to the simulation's definition, which is
            // where a narrower type would have shown up.
            CHECK(parseWeaponDefinition(weapons[0].second).taWeaponId == id);
        }
    }

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
