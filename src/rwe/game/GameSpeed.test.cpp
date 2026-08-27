#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameSpeed.h>

namespace rwe
{
    TEST_CASE("GameSpeed default", "[speed]")
    {
        SECTION("default-constructed is index 5 / 1.0x")
        {
            GameSpeed s;
            REQUIRE(s.index() == GameSpeed::DefaultIndex);
            REQUIRE(s.index() == 5);
            REQUIRE(s.perMille() == 1000);
            REQUIRE(s.isDefault());
            REQUIRE(s.displayOffset() == 0);
        }
    }

    TEST_CASE("GameSpeed clamping", "[speed]")
    {
        SECTION("indices below min clamp to min")
        {
            REQUIRE(GameSpeed(-5).index() == GameSpeed::MinIndex);
            REQUIRE(GameSpeed(-1).index() == 0);
            REQUIRE(GameSpeed(-1).perMille() == 100);
        }

        SECTION("indices above max clamp to max")
        {
            REQUIRE(GameSpeed(99).index() == GameSpeed::MaxIndex);
            REQUIRE(GameSpeed(GameSpeed::MaxIndex + 1).index() == GameSpeed::MaxIndex);
            REQUIRE(GameSpeed(99).perMille() == 5000);
        }

        SECTION("increase at max stays at max")
        {
            GameSpeed s(GameSpeed::MaxIndex);
            REQUIRE(s.increased() == s);
            REQUIRE(s.increased().index() == GameSpeed::MaxIndex);
        }

        SECTION("decrease at min stays at min")
        {
            GameSpeed s(GameSpeed::MinIndex);
            REQUIRE(s.decreased() == s);
            REQUIRE(s.decreased().index() == GameSpeed::MinIndex);
        }
    }

    TEST_CASE("GameSpeed increase/decrease symmetry", "[speed]")
    {
        SECTION("increase then decrease returns to original (in middle range)")
        {
            for (int i = GameSpeed::MinIndex + 1; i < GameSpeed::MaxIndex; ++i)
            {
                GameSpeed s(i);
                REQUIRE(s.increased().decreased() == s);
                REQUIRE(s.decreased().increased() == s);
            }
        }

        SECTION("default speed up moves to next step")
        {
            GameSpeed s;
            REQUIRE(s.increased().index() == 6);
            REQUIRE(s.increased().perMille() == 1500);
            REQUIRE(s.decreased().index() == 4);
            REQUIRE(s.decreased().perMille() == 700);
        }
    }

    TEST_CASE("GameSpeed displayOffset signs", "[speed]")
    {
        SECTION("at default, offset is zero")
        {
            REQUIRE(GameSpeed().displayOffset() == 0);
        }

        SECTION("above default is positive")
        {
            REQUIRE(GameSpeed(GameSpeed::DefaultIndex + 1).displayOffset() == 1);
            REQUIRE(GameSpeed(GameSpeed::MaxIndex).displayOffset() == GameSpeed::MaxIndex - GameSpeed::DefaultIndex);
            REQUIRE(GameSpeed(GameSpeed::MaxIndex).displayOffset() > 0);
        }

        SECTION("below default is negative")
        {
            REQUIRE(GameSpeed(GameSpeed::DefaultIndex - 1).displayOffset() == -1);
            REQUIRE(GameSpeed(GameSpeed::MinIndex).displayOffset() == GameSpeed::MinIndex - GameSpeed::DefaultIndex);
            REQUIRE(GameSpeed(GameSpeed::MinIndex).displayOffset() < 0);
        }

        SECTION("isDefault true only at default index")
        {
            REQUIRE(GameSpeed(GameSpeed::DefaultIndex).isDefault());
            REQUIRE_FALSE(GameSpeed(GameSpeed::DefaultIndex - 1).isDefault());
            REQUIRE_FALSE(GameSpeed(GameSpeed::DefaultIndex + 1).isDefault());
        }
    }

    TEST_CASE("GameSpeed step values", "[speed]")
    {
        SECTION("verified TA speed steps")
        {
            REQUIRE(GameSpeed(0).perMille() == 100);
            REQUIRE(GameSpeed(1).perMille() == 200);
            REQUIRE(GameSpeed(2).perMille() == 300);
            REQUIRE(GameSpeed(3).perMille() == 500);
            REQUIRE(GameSpeed(4).perMille() == 700);
            REQUIRE(GameSpeed(5).perMille() == 1000);
            REQUIRE(GameSpeed(6).perMille() == 1500);
            REQUIRE(GameSpeed(7).perMille() == 2000);
            REQUIRE(GameSpeed(8).perMille() == 3000);
            REQUIRE(GameSpeed(9).perMille() == 5000);
        }
    }

    TEST_CASE("GameSpeed equality", "[speed]")
    {
        SECTION("same index compares equal")
        {
            REQUIRE(GameSpeed(3) == GameSpeed(3));
            REQUIRE_FALSE(GameSpeed(3) != GameSpeed(3));
        }

        SECTION("different indices compare unequal")
        {
            REQUIRE(GameSpeed(3) != GameSpeed(4));
            REQUIRE_FALSE(GameSpeed(3) == GameSpeed(4));
        }

        SECTION("clamped values that equal compare equal")
        {
            REQUIRE(GameSpeed(-5) == GameSpeed(GameSpeed::MinIndex));
            REQUIRE(GameSpeed(99) == GameSpeed(GameSpeed::MaxIndex));
        }
    }
}
