#include <catch2/catch_test_macros.hpp>
#include <rwe/game/panel_slide.h>

namespace rwe
{
    TEST_CASE("F4 latches the players' list out and Space only peeks", "[panelslide]")
    {
        // 76: while the F4 bit is clear a held Space brings the list out --
        // unless the cursor is on the side panel, which is the original's own
        // exception.
        REQUIRE(playerListWantsOut(false, false, false) == false);
        REQUIRE(playerListWantsOut(false, true, false) == true);
        REQUIRE(playerListWantsOut(false, true, true) == false);

        // The latch outranks all of it: F4 set holds the list out whatever
        // Space and the cursor are doing.
        REQUIRE(playerListWantsOut(true, false, false) == true);
        REQUIRE(playerListWantsOut(true, true, true) == true);
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

    TEST_CASE("the camera counter-moves the viewport's half-change", "[panelslide]")
    {
        // Hiding the panel (128 -> 0) widens the view to the left by 128, so
        // the view's centre moves 64 left; the camera moves 64 world units
        // left to hold the world still.
        REQUIRE(panelSlideCameraShift(128, 0, 1.0f) == 64.0f);

        // Bringing it back is the same shift with the opposite sign, so a
        // round trip lands exactly where it started rather than drifting.
        REQUIRE(panelSlideCameraShift(0, 128, 1.0f) == -64.0f);

        // Zoomed in, a screen pixel is less ground, so the same 64-pixel
        // centre change moves the camera through fewer world units.
        REQUIRE(panelSlideCameraShift(128, 0, 2.0f) == 32.0f);

        // No change, no movement.
        REQUIRE(panelSlideCameraShift(128, 128, 1.0f) == 0.0f);
    }
}
