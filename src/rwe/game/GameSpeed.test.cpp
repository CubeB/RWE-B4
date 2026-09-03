#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameSpeed.h>

namespace rwe
{
    TEST_CASE("GameSpeed default", "[speed]")
    {
        SECTION("default-constructed is the middle step, 1.0x")
        {
            // The original's speed control runs -10 to +10 around normal, so
            // the middle of twenty-one steps is 1.0x.
            GameSpeed s;
            REQUIRE(s.index() == GameSpeed::DefaultIndex);
            REQUIRE(s.index() == 9);
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
            REQUIRE(GameSpeed(99).perMille() == 2000);
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
            REQUIRE(s.increased().index() == 10);
            REQUIRE(s.increased().perMille() == 1100);
            REQUIRE(s.decreased().index() == 8);
            REQUIRE(s.decreased().perMille() == 900);
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
            // The original keeps speed as 1..20 and shows value - 10, so
            // the HUD reads -9 to +10 with 0 at normal, and step n is n
            // tenths of normal speed -- a linear ramp, not a geometric one.
            REQUIRE(GameSpeed(0).perMille() == 100);
            REQUIRE(GameSpeed(0).displayOffset() == -9);
            REQUIRE(GameSpeed(GameSpeed::DefaultIndex).perMille() == 1000);
            REQUIRE(GameSpeed(GameSpeed::DefaultIndex).displayOffset() == 0);
            REQUIRE(GameSpeed(GameSpeed::MaxIndex).perMille() == 2000);
            REQUIRE(GameSpeed(GameSpeed::MaxIndex).displayOffset() == 10);

            // Monotonic throughout: every step is faster than the one below.
            for (int i = 1; i <= GameSpeed::MaxIndex; ++i)
            {
                REQUIRE(GameSpeed(i).perMille() > GameSpeed(i - 1).perMille());
            }
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
