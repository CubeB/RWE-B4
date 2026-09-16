#include <catch2/catch_test_macros.hpp>
#include <rwe/game/panel_slide.h>

namespace rwe
{
    TEST_CASE("F4 latches the panel away and Space only peeks", "[panelslide]")
    {
        // 76: the F4 bit alone decides it, and while that bit is clear a held
        // Space hides the panel -- unless the cursor is on the panel itself,
        // which is the original's own exception.
        REQUIRE(panelWantsHiding(false, false, false) == false);
        REQUIRE(panelWantsHiding(false, true, false) == true);
        REQUIRE(panelWantsHiding(false, true, true) == false);

        // The latch outranks all of it: F4 set runs to the hidden endpoint
        // whatever Space and the cursor are doing.
        REQUIRE(panelWantsHiding(true, false, false) == true);
        REQUIRE(panelWantsHiding(true, true, true) == true);
    }

    TEST_CASE("the slide reaches its endpoint exactly and stops there", "[panelslide]")
    {
        // A step that would overshoot lands on the target instead, which is
        // what lets the arrival test be an equality rather than a tolerance --
        // and the arrival is what plays the sound, so it must happen once.
        REQUIRE(advancePanelSlide(0.0f, 128.0f, 850.0f, 1000) == 128.0f);
        REQUIRE(advancePanelSlide(128.0f, 0.0f, 850.0f, 1000) == 0.0f);

        // Already there: nothing moves, so nothing re-triggers.
        REQUIRE(advancePanelSlide(128.0f, 128.0f, 850.0f, 16) == 128.0f);
        REQUIRE(advancePanelSlide(0.0f, 0.0f, 850.0f, 16) == 0.0f);

        // A frame's worth of travel goes the right way on both journeys.
        auto out = advancePanelSlide(0.0f, 128.0f, 850.0f, 16);
        REQUIRE(out > 0.0f);
        REQUIRE(out < 128.0f);
        auto back = advancePanelSlide(128.0f, 0.0f, 850.0f, 16);
        REQUIRE(back < 128.0f);
        REQUIRE(back > 0.0f);

        // The whole travel is crossed in about a sixth of a second at the
        // rate GameScene uses, so the slide cannot stall part-way at any
        // frame rate the game runs at.
        auto p = 0.0f;
        for (int i = 0; i < 10; ++i)
        {
            p = advancePanelSlide(p, 128.0f, 850.0f, 16);
        }
        REQUIRE(p == 128.0f);
    }
}
