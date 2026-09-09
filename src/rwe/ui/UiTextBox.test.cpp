#include <catch2/catch_test_macros.hpp>
#include <rwe/ui/UiTextBox.h>

#include <SDL3/SDL.h>

namespace rwe
{
    TEST_CASE("textBoxCharacterFor")
    {
        SECTION("a letter with no modifier comes out lowercase")
        {
            REQUIRE(textBoxCharacterFor(SDLK_A, 0) == 'a');
            REQUIRE(textBoxCharacterFor(SDLK_Z, 0) == 'z');
        }

        SECTION("shift makes a letter uppercase")
        {
            REQUIRE(textBoxCharacterFor(SDLK_A, SDL_KMOD_SHIFT) == 'A');
        }

        SECTION("caps lock alone also makes a letter uppercase")
        {
            REQUIRE(textBoxCharacterFor(SDLK_A, SDL_KMOD_CAPS) == 'A');
        }

        SECTION("caps lock plus shift cancel out, back to lowercase")
        {
            REQUIRE(textBoxCharacterFor(SDLK_A, static_cast<unsigned short>(SDL_KMOD_SHIFT | SDL_KMOD_CAPS)) == 'a');
        }

        SECTION("each digit, unshifted and shifted")
        {
            REQUIRE(textBoxCharacterFor(SDLK_1, 0) == '1');
            REQUIRE(textBoxCharacterFor(SDLK_1, SDL_KMOD_SHIFT) == '!');
            REQUIRE(textBoxCharacterFor(SDLK_2, 0) == '2');
            REQUIRE(textBoxCharacterFor(SDLK_2, SDL_KMOD_SHIFT) == '@');
            REQUIRE(textBoxCharacterFor(SDLK_3, 0) == '3');
            REQUIRE(textBoxCharacterFor(SDLK_3, SDL_KMOD_SHIFT) == '#');
            REQUIRE(textBoxCharacterFor(SDLK_4, 0) == '4');
            REQUIRE(textBoxCharacterFor(SDLK_4, SDL_KMOD_SHIFT) == '$');
            REQUIRE(textBoxCharacterFor(SDLK_5, 0) == '5');
            REQUIRE(textBoxCharacterFor(SDLK_5, SDL_KMOD_SHIFT) == '%');
            REQUIRE(textBoxCharacterFor(SDLK_6, 0) == '6');
            REQUIRE(textBoxCharacterFor(SDLK_6, SDL_KMOD_SHIFT) == '^');
            REQUIRE(textBoxCharacterFor(SDLK_7, 0) == '7');
            REQUIRE(textBoxCharacterFor(SDLK_7, SDL_KMOD_SHIFT) == '&');
            REQUIRE(textBoxCharacterFor(SDLK_8, 0) == '8');
            REQUIRE(textBoxCharacterFor(SDLK_8, SDL_KMOD_SHIFT) == '*');
            REQUIRE(textBoxCharacterFor(SDLK_9, 0) == '9');
            REQUIRE(textBoxCharacterFor(SDLK_9, SDL_KMOD_SHIFT) == '(');
            REQUIRE(textBoxCharacterFor(SDLK_0, 0) == '0');
            REQUIRE(textBoxCharacterFor(SDLK_0, SDL_KMOD_SHIFT) == ')');
        }

        SECTION("shift plus minus is an underscore, which a save name needs")
        {
            REQUIRE(textBoxCharacterFor(SDLK_MINUS, 0) == '-');
            REQUIRE(textBoxCharacterFor(SDLK_MINUS, SDL_KMOD_SHIFT) == '_');
        }

        SECTION("a key not in the table is ignored")
        {
            REQUIRE(textBoxCharacterFor(SDLK_F1, 0) == std::nullopt);
            REQUIRE(textBoxCharacterFor(SDLK_BACKSPACE, 0) == std::nullopt);
        }
    }
}
