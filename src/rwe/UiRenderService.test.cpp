#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/UiRenderService.h>
#include <rwe/render/SpriteSeries.h>

namespace rwe
{
    namespace
    {
        /**
         * A font of 128 glyphs whose advances are the character's own width
         * in the shipped Haettenschweiler: proportional, so a wrong offset
         * cannot pass by accident the way it would against a fixed pitch.
         * Nothing here is drawn, so the sprites carry no texture and no mesh.
         */
        SpriteSeries makeFont()
        {
            SpriteSeries font;
            for (int ch = 0; ch < 128; ++ch)
            {
                // 'l' and 'i' are the narrow ones, 'M' and 'W' the wide.
                float width = 7.0f;
                if (ch == 'l' || ch == 'i' || ch == 'I')
                {
                    width = 5.0f;
                }
                else if (ch == 'M' || ch == 'W' || ch == 'm' || ch == 'w')
                {
                    width = 11.0f;
                }
                else if (ch == ' ')
                {
                    width = 4.0f;
                }

                font.sprites.push_back(std::make_shared<Sprite>(
                    Rectangle2f::fromTopLeft(0.0f, -11.0f, width, 12.0f),
                    SharedTextureHandle(),
                    nullptr));
            }
            return font;
        }
    }

    TEST_CASE("findCharacterInText locates the quick key in a caption", "[ui]")
    {
        // What the quick-key underline is measured with (S:99, 0x4A5B2C):
        // the same per-glyph advances drawText steps by, so the line lands
        // under the character the caption actually drew.
        auto font = makeFont();

        SECTION("the first character of a caption is at zero")
        {
            auto span = findCharacterInText("SINGLE", 's', font);
            REQUIRE(span.has_value());
            REQUIRE(span->x == Catch::Approx(0.0f));
            REQUIRE(span->width == Catch::Approx(7.0f));
        }

        SECTION("a later character is offset by everything in front of it")
        {
            // SKIRMISH.GUI's SelectMap: caption "Select Map", quickkey 'e',
            // which is the second character, one 'S' along.
            auto span = findCharacterInText("Select Map", 'e', font);
            REQUIRE(span.has_value());
            REQUIRE(span->x == Catch::Approx(7.0f));
            REQUIRE(span->width == Catch::Approx(7.0f));
        }

        SECTION("the advances are the font's own, not a fixed pitch")
        {
            // "Mil" is 11 + 5, so the 'l' starts at 16 and is 5 wide.
            auto span = findCharacterInText("Mil", 'l', font);
            REQUIRE(span.has_value());
            REQUIRE(span->x == Catch::Approx(16.0f));
            REQUIRE(span->width == Catch::Approx(5.0f));
        }

        SECTION("case does not matter, in either direction")
        {
            // RWE has folded the gui file's quickkey to an SDL keycode by the
            // time a button holds it, so the original's case is gone; and
            // four shipped gadgets are authored in the other case anyway.
            REQUIRE(findCharacterInText("SINGLE", 's', font)->x == Catch::Approx(0.0f));
            // "Undo Changes": U n d o are 7 each and the space is 4, so the
            // 'C' starts at 32. "Select Mission": S and e are 7 each, so the
            // 'l' starts at 14.
            REQUIRE(findCharacterInText("Undo Changes", 'c', font)->x == Catch::Approx(32.0f));
            REQUIRE(findCharacterInText("Select Mission", 'L', font)->x == Catch::Approx(14.0f));
        }

        SECTION("a character that is not there gets no underline")
        {
            REQUIRE(!findCharacterInText("SINGLE", 'q', font).has_value());
        }

        SECTION("a gadget with no quick key at all asks with zero")
        {
            // MAINMENU.GUI's Credits gadget carries quickkey=0, which becomes
            // keycode 0; there is no such character and there must be no
            // underline.
            REQUIRE(!findCharacterInText("SINGLE", 0, font).has_value());
        }

        SECTION("an empty caption has nothing to underline")
        {
            REQUIRE(!findCharacterInText("", 's', font).has_value());
        }
    }

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
