#include <array>
#include <catch2/catch_test_macros.hpp>
#include <rwe/UiRenderService.h>
#include <rwe/ui/UiLabel.h>

namespace rwe
{
    namespace
    {
        std::shared_ptr<SpriteSeries> labelFontFixture()
        {
            // MAP-FONT-01/02: frame widths of ASCII 32..126, read from shipped
            // hattfont11.GAF. These test widths, not synthetic fixed-pitch
            // approximations, determine the five original line breaks.
            constexpr std::array<unsigned char, 95> widths{
                7,4,5,11,5,9,6,4,5,5,7,5,4,5,3,6,6,5,6,6,7,6,6,6,5,5,3,3,7,5,7,6,
                11,7,6,6,6,5,5,6,6,3,5,6,5,7,7,6,6,6,6,6,6,6,7,8,6,6,5,4,6,4,7,7,
                3,6,6,6,6,6,5,6,6,4,4,6,3,8,6,6,6,6,6,6,5,6,6,9,5,6,6,6,4,6,8};
            auto font = std::make_shared<SpriteSeries>();
            for (unsigned int ch = 0; ch < 128; ++ch)
            {
                const auto width = ch >= 32 && ch <= 126 ? widths[ch - 32] : 0;
                // Ink bearings are immaterial to this width fixture except
                // for '.', whose actual +1 placement guards the advance bug.
                const auto bounds = ch == '.' ? Rectangle2f::fromTopLeft(1,-2,3,3)
                    : Rectangle2f::fromTopLeft(0,-9,width,10);
                font->sprites.push_back(std::make_shared<Sprite>(bounds,SharedTextureHandle{},nullptr));
            }
            font->textBaseline = 10;
            font->textLineAdvance = 14;
            font->advanceByFrameWidth = true;
            return font;
        }

        std::string withoutSpaces(const std::string& text)
        {
            std::string result;
            for (auto ch : text) if (ch != ' ') result += ch;
            return result;
        }
    }

    TEST_CASE("original GUI label font fits the five reported map descriptions", "[ui][text][map-font]")
    {
        // Independent prose row widths and breaks from MAP-FONT-02, including
        // the exactly-235 first row and the authored double spaces.
        struct Fixture { const char* name; const char* first; int firstWidth; const char* second; int secondWidth; };
        const Fixture fixtures[]{
            {"Mounds of Mars","8 X 17  For more than two players, play",235,"teams, North vs. South.",133},
            {"Ror Shock","26 X 26 Ocean Battles are a must for",219,"this fine map.  Big surprise in center!",225},
            {"Steel Jungle","20 x 10  Wake up and smell the coffee",224,"with the Morning War Show.",160},
            {"Temblorian Mist","12 X 15  Make your way around the",207,"edges or fly across the center.",182},
            {"The Barrier Reef","19 X 19  Body of water separates two",222,"land masses.  Great underwater starts!",226}};
        const auto font = labelFontFixture();
        UiRenderService measure(nullptr,nullptr,nullptr);
        for (const auto& f : fixtures)
        {
            CAPTURE(f.name);
            const auto text = std::string(f.first) + " " + f.second;
            const auto glyphs = layoutWrappedText(Rectangle2f::fromTopLeft(0,0,235,31),text,*font);
            const auto unlimited = layoutWrappedText(Rectangle2f::fromTopLeft(0,0,235,100000),text,*font);
            REQUIRE(glyphs.size() == unlimited.size());
            std::string rows[2];
            float ends[2]{};
            for (const auto& g : glyphs)
            {
                REQUIRE((g.y == 10 || g.y == 24));
                const int row = g.y == 10 ? 0 : 1;
                rows[row] += static_cast<char>(g.glyph);
                ends[row] = g.x + font->glyphAdvance(g.glyph);
            }
            CHECK(rows[0] == withoutSpaces(f.first));
            CHECK(rows[1] == withoutSpaces(f.second));
            CHECK(ends[0] == f.firstWidth);
            CHECK(ends[1] == f.secondWidth);
            CHECK(measure.getTextWidth(f.first,*font) == f.firstWidth);
            CHECK(measure.getTextWidth(f.second,*font) == f.secondWidth);
            UiLabel label(60,300,235,31,text,font);
            CHECK(label.getTextHeight() <= 31);
        }
    }

    TEST_CASE("label advances use GAF frame width without shifting its ink bearing", "[ui][text][map-font]")
    {
        const auto font = labelFontFixture();
        UiRenderService measure(nullptr,nullptr,nullptr);
        CHECK(font->sprites['.']->bounds.left() == 1);
        CHECK(font->sprites['.']->bounds.right() == 4);
        CHECK(font->glyphAdvance('.') == 3);
        CHECK(measure.getTextWidth("I.I",*font) == 9);
        const auto span = findCharacterInText("I.I",'.',*font);
        REQUIRE(span);
        CHECK(span->x == 3);
        CHECK(span->width == 3);
    }
}
