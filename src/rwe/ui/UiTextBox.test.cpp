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
}
