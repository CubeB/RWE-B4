#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/game/ReloadBar.h>

namespace rwe
{
    namespace
    {
        WeaponDefinition reloadBarWeapon(float reloadSeconds, bool reloadBar = true)
        {
            WeaponDefinition definition{};
            definition.reloadTime = SimScalar(reloadSeconds);
            definition.reloadBar = reloadBar;
            return definition;
        }

        WeaponDefinition& addWeapon(
            std::unordered_map<std::string, WeaponDefinition>& definitions,
            const std::string& name,
            float reloadSeconds,
            bool reloadBar = true)
        {
            return definitions.emplace(name, reloadBarWeapon(reloadSeconds, reloadBar)).first->second;
        }
    }

    TEST_CASE("the reload bar follows the slowest tagged weapon", "[game][reloadbar]")
    {
        std::unordered_map<std::string, WeaponDefinition> definitions;
        addWeapon(definitions, "FAST", 1.0f);
        addWeapon(definitions, "UNTAGGED", 20.0f, false);
        addWeapon(definitions, "SLOW", 5.0f);

        std::array<std::optional<UnitWeapon>, 3> weapons;
        weapons[0] = UnitWeapon{};
        weapons[0]->weaponType = "FAST";
        weapons[1] = UnitWeapon{};
        weapons[1]->weaponType = "UNTAGGED";
        weapons[2] = UnitWeapon{};
        weapons[2]->weaponType = "SLOW";

        SECTION("a longer reload wins even when it has not fired")
        {
            weapons[2]->readyTime = GameTime(0);

            const auto selected = chooseReloadBarWeapon(weapons, definitions, 100, 100, 0);
            REQUIRE(selected);
            CHECK(selected->weaponIndex == 2);
            CHECK(selected->reloadTicks == GameTime(150));
            CHECK(selected->readyTime == GameTime(0));
        }

        SECTION("a tag is required")
        {
            weapons[2]->weaponType = "UNTAGGED";
            definitions.at("FAST").reloadBar = false;

            CHECK_FALSE(chooseReloadBarWeapon(weapons, definitions, 100, 100, 0));
        }

        SECTION("the stored reload includes damage and veterancy scaling")
        {
            weapons[2]->readyTime = GameTime(400);

            // 150 base ticks, at half health (x110%) and 25 kills (x70%).
            const auto selected = chooseReloadBarWeapon(weapons, definitions, 100, 200, 25);
            REQUIRE(selected);
            CHECK(selected->weaponIndex == 2);
            CHECK(selected->reloadTicks == GameTime(115));
        }

        SECTION("an unknown weapon definition is skipped")
        {
            weapons[2]->weaponType = "MISSING";

            const auto selected = chooseReloadBarWeapon(weapons, definitions, 100, 100, 0);
            REQUIRE(selected);
            CHECK(selected->weaponIndex == 0);
        }
    }

    TEST_CASE("reload bar progress is full before firing and empties at the shot", "[game][reloadbar]")
    {
        ReloadBarState state{0, GameTime(160), GameTime(150)};

        SECTION("full when the weapon is ready before any shot")
        {
            const ReloadBarState ready{0, GameTime(0), GameTime(150)};
            CHECK(reloadBarProgress(ready, GameTime(0)) == 1.0f);
            CHECK(reloadBarProgress(ready, GameTime(100)) == 1.0f);
        }

        SECTION("empty on the tick the weapon fires")
        {
            CHECK(reloadBarProgress(state, GameTime(10)) == 0.0f);
        }

        SECTION("refills over the reload time")
        {
            CHECK(reloadBarProgress(state, GameTime(85)) == Catch::Approx(0.5f));
            CHECK(reloadBarProgress(state, GameTime(159)) == Catch::Approx(149.0f / 150.0f));
            CHECK(reloadBarProgress(state, GameTime(160)) == 1.0f);
        }

        SECTION("clamps a ready time further away than one reload")
        {
            CHECK(reloadBarProgress(ReloadBarState{0, GameTime(500), GameTime(150)}, GameTime(0)) == 0.0f);
        }
    }
}
