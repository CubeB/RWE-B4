#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/UiRenderService.h>

namespace rwe
{
    TEST_CASE("computeUiOrthoBounds keeps a fixed-size UI in proportion", "[ui]")
    {
        // The menus are laid out at 640x480 because that is what the GUI
        // files carry. The projection used to map that onto whatever shape
        // the window was, so a widescreen display stretched every button.
        const float w = 640.0f;
        const float h = 480.0f;

        SECTION("a window already at the content's aspect is left alone")
        {
            auto b = computeUiOrthoBounds(w, h, 1024.0f, 768.0f);
            REQUIRE(b.left == Catch::Approx(0.0f));
            REQUIRE(b.right == Catch::Approx(640.0f));
            REQUIRE(b.top == Catch::Approx(0.0f));
            REQUIRE(b.bottom == Catch::Approx(480.0f));
        }

        SECTION("a wider window gains the slack on the sides, evenly")
        {
            // 16:9 wants 480 * 16/9 = 853.33 across, so 106.67 each side.
            auto b = computeUiOrthoBounds(w, h, 1920.0f, 1080.0f);
            REQUIRE(b.left == Catch::Approx(-106.6667f));
            REQUIRE(b.right == Catch::Approx(746.6667f));
            REQUIRE(b.top == Catch::Approx(0.0f));
            REQUIRE(b.bottom == Catch::Approx(480.0f));

            // Which is the whole point: a pixel is as wide as it is tall.
            REQUIRE((b.right - b.left) / (b.bottom - b.top) == Catch::Approx(1920.0f / 1080.0f));
        }

        SECTION("a taller window gains it above and below")
        {
            // 1:1 wants 640 down, so 80 top and bottom.
            auto b = computeUiOrthoBounds(w, h, 800.0f, 800.0f);
            REQUIRE(b.left == Catch::Approx(0.0f));
            REQUIRE(b.right == Catch::Approx(640.0f));
            REQUIRE(b.top == Catch::Approx(-80.0f));
            REQUIRE(b.bottom == Catch::Approx(560.0f));
            REQUIRE((b.right - b.left) / (b.bottom - b.top) == Catch::Approx(1.0f));
        }

        SECTION("the content stays centred, whatever the window")
        {
            for (auto windowWidth : {800.0f, 1280.0f, 1920.0f, 2560.0f, 400.0f})
            {
                auto b = computeUiOrthoBounds(w, h, windowWidth, 1080.0f);
                INFO("window width " << windowWidth);
                REQUIRE((b.left + b.right) / 2.0f == Catch::Approx(320.0f));
                REQUIRE((b.top + b.bottom) / 2.0f == Catch::Approx(240.0f));
            }
        }

        SECTION("a degenerate window is the content's own box, not a divide by zero")
        {
            // Minimising a window on Windows reports a height of zero.
            auto b = computeUiOrthoBounds(w, h, 1920.0f, 0.0f);
            REQUIRE(b.left == Catch::Approx(0.0f));
            REQUIRE(b.right == Catch::Approx(640.0f));
            REQUIRE(b.top == Catch::Approx(0.0f));
            REQUIRE(b.bottom == Catch::Approx(480.0f));
        }
    }
}
