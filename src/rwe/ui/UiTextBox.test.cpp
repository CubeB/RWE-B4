#include <catch2/catch_test_macros.hpp>
#include <rwe/ui/UiTextBox.h>

#include <SDL3/SDL.h>
#include <string>

namespace rwe
{
    namespace
    {
        UiTextBox makeBox()
        {
            // No font: nothing here draws, and the box needs one only to
            // render.
            return UiTextBox(0, 0, 100, 12, "", nullptr);
        }

        /** A face with only the "I" frame's height made interesting. */
        std::shared_ptr<SpriteSeries> textBoxFont(int iFrameHeight)
        {
            auto font = std::make_shared<SpriteSeries>();
            font->sprites.resize(256);
            for (auto& sprite : font->sprites)
            {
                sprite = std::make_shared<Sprite>(Rectangle2f::fromTopLeft(0.0f, 0.0f, 5.0f, 12.0f), SharedTextureHandle(), nullptr);
            }
            font->sprites['I'] = std::make_shared<Sprite>(
                Rectangle2f::fromTopLeft(0.0f, 0.0f, 5.0f, static_cast<float>(iFrameHeight)), SharedTextureHandle(), nullptr);
            return font;
        }
    }

    TEST_CASE("a text box's line is the pen plus the I height, whatever its height", "[ui]")
    {
        // #903, BT-GUI-TEXTBOX-BASELINE-FINDINGS.md: the original's type-3
        // draw puts the pen three rows below the gadget top and normalises
        // glyphs by the "I" height; it does not centre by the box's height.
        // The shipped LOUNGE2 MESSAGE box is 20 high at y 441 in hattfont12
        // (I = 12): drawer row 456, caret rows 444-458.
        const auto font = textBoxFont(12);
        const auto box = UiTextBox(21, 441, 294, 20, "", font);
        REQUIRE(box.textLineBaseline() == 456.0f);
        REQUIRE(box.caretTop() == 444.0f);
        REQUIRE(box.caretHeight() == 15.0f);

        SECTION("the box's own height does not move the text")
        {
            const auto tall = UiTextBox(21, 441, 294, 28, "", font);
            REQUIRE(tall.textLineBaseline() == 456.0f);
        }

        SECTION("a face whose I is a different height uses that height")
        {
            const auto shortFace = UiTextBox(21, 441, 294, 20, "", textBoxFont(10));
            REQUIRE(shortFace.textLineBaseline() == 454.0f);
            REQUIRE(shortFace.caretHeight() == 13.0f);
        }
    }

    TEST_CASE("a text box is written from composed text, not from keycodes", "[ui]")
    {
        // It used to map keycodes through a hand-written US table, so a
        // player on any other layout got the wrong characters in a save name.
        // SDL composes the text for whatever layout is in use and the box
        // takes that; the keyboard handler keeps only the editing keys.
        SECTION("typing appends")
        {
            auto box = makeBox();
            box.textInput("s");
            box.textInput("a");
            box.textInput("ve");
            REQUIRE(box.getText() == "save");
        }

        SECTION("what arrives is kept exactly, punctuation and case alike")
        {
            auto box = makeBox();
            box.textInput("My_Game-2!");
            REQUIRE(box.getText() == "My_Game-2!");
        }

        SECTION("backspace still deletes, and on an empty box does nothing")
        {
            auto box = makeBox();
            box.textInput("ab");
            box.keyDown(KeyEvent(SDLK_BACKSPACE));
            REQUIRE(box.getText() == "a");
            box.keyDown(KeyEvent(SDLK_BACKSPACE));
            box.keyDown(KeyEvent(SDLK_BACKSPACE));
            REQUIRE(box.getText() == "");
        }

        SECTION("a letter key on its own writes nothing")
        {
            // The characters come from text input now. A key that arrives
            // with no text behind it -- a function key, or a letter on a
            // layout whose composition SDL has not finished -- is not one.
            auto box = makeBox();
            box.keyDown(KeyEvent(SDLK_A));
            box.keyDown(KeyEvent(SDLK_F5));
            REQUIRE(box.getText() == "");
        }

        SECTION("the box stops at its length, mid-string if need be")
        {
            auto box = makeBox();
            box.textInput(std::string(30, 'x'));
            REQUIRE(box.getText().size() == 24);
        }

        SECTION("anything outside printable ASCII is dropped")
        {
            // A save name is a filename and the font has one glyph to a byte,
            // so the UTF-8 SDL hands over is filtered rather than stored as
            // bytes nothing can draw.
            // The two bytes of an e-acute, built rather than written as an
            // escape: a hex escape in a string literal eats every hex digit
            // that follows it, so "\xa9b" is one number and not a byte and a
            // letter.
            std::string typed("a");
            typed.push_back(static_cast<char>(0xc3));
            typed.push_back(static_cast<char>(0xa9));
            typed.push_back('b');

            auto box = makeBox();
            box.textInput(typed);
            REQUIRE(box.getText() == "ab");
        }

        SECTION("a box says it is typed into")
        {
            auto box = makeBox();
            REQUIRE(box.wantsTextInput());
        }
    }

    TEST_CASE("a text box can be limited, masked and made read-only", "[ui]")
    {
        // The multiplayer screens are NEWMULTI.GUI and SELGAME.GUI, whose
        // gadgets say how long a name may be (ten, in NEWMULTI) and whose
        // password is never shown.
        SECTION("a length set from the gui file caps typing")
        {
            auto box = makeBox();
            box.setMaxLength(10);
            box.textInput("abcdefghijklmnop");
            REQUIRE(box.getText() == "abcdefghij");
        }

        SECTION("a longer limit than the default is honoured")
        {
            auto box = makeBox();
            box.setMaxLength(63);
            box.textInput(std::string(70, 'x'));
            REQUIRE(box.getText().size() == 63);
        }

        SECTION("lowering the limit trims what is already there")
        {
            auto box = makeBox();
            box.textInput("hello world");
            box.setMaxLength(5);
            REQUIRE(box.getText() == "hello");
        }

        SECTION("a masked box keeps what was typed")
        {
            auto box = makeBox();
            box.setMasked(true);
            box.textInput("secret");
            REQUIRE(box.isMasked());
            REQUIRE(box.getText() == "secret");
        }

        SECTION("a read-only box takes neither text nor backspace")
        {
            auto box = makeBox();
            box.setText("192.168.0.5:1337");
            box.setEditable(false);
            box.textInput("x");
            box.keyDown(KeyEvent(SDLK_BACKSPACE));
            REQUIRE(box.getText() == "192.168.0.5:1337");

            // ...but the scene can still write it, which is how a joined
            // game's addresses follow the host's announcement.
            box.setText("10.0.0.2:1337");
            REQUIRE(box.getText() == "10.0.0.2:1337");
        }
    }
}
