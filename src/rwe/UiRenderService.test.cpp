#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/UiRenderService.h>
#include <rwe/math/Vector3f.h>
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

    TEST_CASE("map descriptions stay above the size and player count", "[ui][text]")
    {
        // Actual descriptions from ccmaps.ccx, in SELMAP.GUI's 235x31 label.
        // SIZE begins at y=330, thirty pixels below DESCRIPTION.
        const std::vector<std::string> descriptions{
            "19 X 19  Body of water separates two land masses.  Great underwater starts!",
            "12 X 15  Make your way around the edges or fly across the center.",
            "20 X 20  Large polar ice cap map. A bleak and forbidding world.",
        };
        const auto font = makeFont();
        const auto area = Rectangle2f::fromTopLeft(60, 300, 235, 30);
        for (const auto& description : descriptions)
        {
            const auto glyphs = layoutWrappedText(area, description, font);
            REQUIRE_FALSE(glyphs.empty());
            for (const auto& glyph : glyphs)
            {
                const auto& bounds = font.sprites[glyph.glyph]->bounds;
                CHECK(glyph.x + bounds.left() >= area.left());
                CHECK(glyph.x + bounds.right() <= area.right());
                CHECK(glyph.y + bounds.top() >= area.top());
                CHECK(glyph.y + bounds.bottom() < 330);
            }
        }
    }

    TEST_CASE("wrapped labels split long words and respect explicit line breaks", "[ui][text]")
    {
        const auto font = makeFont();
        const auto area = Rectangle2f::fromTopLeft(0, 0, 21, 30);
        const auto glyphs = layoutWrappedText(area, "abcdefghij", font);
        REQUIRE(glyphs.size() == 6);
        CHECK(glyphs.front().y == 12);
        CHECK(glyphs[3].y == 27);
        const auto lines = layoutWrappedText(area, "a\r\nb\nc", font);
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].glyph == 'a');
        CHECK(lines[1].glyph == 'b');
        CHECK(layoutWrappedText(Rectangle2f::fromTopLeft(0, 0, 21, 0), "text", font).empty());
    }

    TEST_CASE("text with a byte that is not valid UTF-8 is measured, not thrown out of the frame", "[ui][malformed]")
    {
        // 0xE9 is "e acute" in Windows-1252, which is how 309 of the 435
        // shipped FBIs carry their accented words. The checked UTF-8 iterator
        // throws on it, from inside the render call.
        auto font = makeFont();
        UiRenderService service(nullptr, nullptr, nullptr);

        std::string accented = "caf";
        accented.push_back(static_cast<char>(0xE9));

        // c, a, f at 7 each, then the Latin-1 character, which this 128-glyph
        // font does not have and so draws as glyph 0 (7 wide).
        REQUIRE_NOTHROW(service.getTextWidth(accented, font));
        REQUIRE(service.getTextWidth(accented, font) == Catch::Approx(28.0f));
        REQUIRE_NOTHROW(findCharacterInText(accented, 'c', font));
    }

    TEST_CASE("the health bar's colour compares with the integer thirds (BT-146)", "[ui]")
    {
        const Color green(83, 223, 79);
        const Color yellow(247, 227, 103);
        const Color red(255, 71, 0);
        auto sameColor = [](const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; };

        // A maximum divisible by three: the third is exactly 100, and health
        // exactly at a third is red, exactly at two thirds yellow.
        CHECK(sameColor(healthBarColor(300, 300), green));
        CHECK(sameColor(healthBarColor(201, 300), green));
        CHECK(sameColor(healthBarColor(200, 300), yellow));
        CHECK(sameColor(healthBarColor(101, 300), yellow));
        CHECK(sameColor(healthBarColor(100, 300), red));
        CHECK(sameColor(healthBarColor(1, 300), red));

        // Not divisible: the third is truncated (100 of 301), so health 100 is
        // red and 101 yellow, 200 yellow and 201 green.
        CHECK(sameColor(healthBarColor(100, 301), red));
        CHECK(sameColor(healthBarColor(101, 301), yellow));
        CHECK(sameColor(healthBarColor(200, 301), yellow));
        CHECK(sameColor(healthBarColor(201, 301), green));
    }

    TEST_CASE("the health bar fills one pixel plus 32 times health over maximum (BT-146)", "[ui]")
    {
        CHECK(healthBarFill(100, 100) == 33);
        CHECK(healthBarFill(1, 100) == 1);
        CHECK(healthBarFill(50, 100) == 17);
        CHECK(healthBarFill(99, 100) == 32);
        // Above the maximum the bar stays full rather than running past its frame.
        CHECK(healthBarFill(150, 100) == 33);
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

    TEST_CASE("UiRenderService UI scale", "[ui]")
    {
        // These only ask the view-projection, which does not touch the
        // graphics context, so null services are enough.
        Viewport frame(0, 0, 1280, 960);
        UiRenderService service(nullptr, nullptr, &frame);

        SECTION("at 1x the bounds and projection are the viewport's own")
        {
            auto b = service.getOrthoBounds();
            REQUIRE(b.left == Catch::Approx(0.0f));
            REQUIRE(b.right == Catch::Approx(1280.0f));
            REQUIRE(b.top == Catch::Approx(0.0f));
            REQUIRE(b.bottom == Catch::Approx(960.0f));

            auto expected = Matrix4f::orthographicProjection(0.0f, 1280.0f, 960.0f, 0.0f, 100.0f, -100.0f);
            REQUIRE(service.getViewProjectionMatrix() == expected);
        }

        SECTION("a scale below one clamps to one")
        {
            service.setUiScale(0.0f);
            REQUIRE(service.getViewProjectionMatrix() == Matrix4f::orthographicProjection(0.0f, 1280.0f, 960.0f, 0.0f, 100.0f, -100.0f));
        }

        SECTION("no aspect viewport: the raw centre maps to the physical centre at 2x")
        {
            service.setUiScale(2.0f);
            // The box is unchanged; the scale lives in the matrix, so raw
            // coordinates up to viewport/uiScale fill the frame.
            auto b = service.getOrthoBounds();
            REQUIRE(b.right == Catch::Approx(1280.0f));
            REQUIRE(b.bottom == Catch::Approx(960.0f));

            // Raw content up to viewport/uiScale fills the frame, so the raw
            // centre is half of that.
            auto clip = service.getViewProjectionMatrix() * Vector3f(320.0f, 240.0f, 0.0f);
            REQUIRE(clip.x == Catch::Approx(0.0f));
            REQUIRE(clip.y == Catch::Approx(0.0f));
        }

        SECTION("no aspect viewport: the inverse round trips a raw point at 3x")
        {
            service.setUiScale(3.0f);
            auto forward = service.getViewProjectionMatrix();
            auto inverse = service.getInverseViewProjectionMatrix();

            Vector3f raw(123.0f, 234.0f, 0.0f);
            auto back = inverse * (forward * raw);
            REQUIRE(back.x == Catch::Approx(raw.x));
            REQUIRE(back.y == Catch::Approx(raw.y));
        }

        SECTION("an aspect viewport ignores the scale")
        {
            Viewport content(0, 0, 640, 480);
            Viewport window(0, 0, 1920, 1080);
            UiRenderService aspect(nullptr, nullptr, &content, &window);
            auto unscaled = aspect.getViewProjectionMatrix();
            aspect.setUiScale(2.0f);

            // The content already fills the window; a scale on top of the
            // fit would draw it past the edges.
            REQUIRE(aspect.getViewProjectionMatrix() == unscaled);
            auto b = aspect.getOrthoBounds();
            REQUIRE(b.top == Catch::Approx(0.0f));
            REQUIRE(b.bottom == Catch::Approx(480.0f));
        }
    }
}
