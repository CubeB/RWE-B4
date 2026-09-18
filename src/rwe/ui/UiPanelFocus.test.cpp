#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include <memory>
#include <rwe/ui/UiPanel.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/ui/UiTextBox.h>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        std::unique_ptr<UiStagedButton> makeButton(const std::string& name)
        {
            // One stage with no art in it: the button refuses to be built
            // with none, and nothing here draws.
            std::vector<UiStagedButton::StageInfo> stages;
            stages.emplace_back(nullptr, std::string());

            auto button = std::make_unique<UiStagedButton>(0, 0, 10, 10, std::move(stages), nullptr, nullptr);
            button->setName(name);
            return button;
        }

        std::unique_ptr<UiTextBox> makeBox(const std::string& name)
        {
            auto box = std::make_unique<UiTextBox>(0, 0, 100, 12, "", nullptr);
            box->setName(name);
            return box;
        }

        KeyEvent key(SDL_Keycode k)
        {
            return KeyEvent(static_cast<int>(k));
        }
    }

    // A panel used to hand every key to every child. That made Space press
    // every button on the panel at once, and would have written the same
    // typing into every text box on a dialog. The original keeps one focused
    // gadget per panel (`panel+0x64`, S:99) and that is what these pin.
    TEST_CASE("a panel gives a key to the control that has the focus", "[ui]")
    {
        auto firstPresses = 0;
        auto secondPresses = 0;

        UiPanel panel(0, 0, 640, 480);
        {
            auto first = makeButton("FIRST");
            auto second = makeButton("SECOND");
            first->addSubscription(first->onClick().subscribe([&firstPresses](const ButtonClickEvent&) { ++firstPresses; }));
            second->addSubscription(second->onClick().subscribe([&secondPresses](const ButtonClickEvent&) { ++secondPresses; }));
            second->setQuickKey(static_cast<int>(SDLK_X));
            panel.appendChild(std::move(first));
            panel.appendChild(std::move(second));
        }

        SECTION("Space presses the focused button and nothing else")
        {
            panel.setFocusByName("FIRST");
            panel.keyDown(key(SDLK_SPACE));

            REQUIRE(firstPresses == 1);
            REQUIRE(secondPresses == 0);
        }

        SECTION("with nothing focused, Space presses nothing")
        {
            panel.clearFocus();
            panel.keyDown(key(SDLK_SPACE));

            REQUIRE(firstPresses == 0);
            REQUIRE(secondPresses == 0);
        }

        SECTION("a quick key presses its own button wherever the focus is")
        {
            // S:78: a quickkey is a panel-wide binding, not a focus one.
            panel.setFocusByName("FIRST");
            panel.keyDown(key(SDLK_X));

            REQUIRE(secondPresses == 1);
            REQUIRE(firstPresses == 0);
        }
    }

    TEST_CASE("a focused text box owns the keyboard", "[ui]")
    {
        auto presses = 0;

        UiPanel panel(0, 0, 640, 480);
        {
            auto button = makeButton("OK");
            button->addSubscription(button->onClick().subscribe([&presses](const ButtonClickEvent&) { ++presses; }));
            button->setQuickKey(static_cast<int>(SDLK_X));
            panel.appendChild(std::move(button));
            panel.appendChild(makeBox("NAME"));
            panel.appendChild(makeBox("OTHER"));
        }
        panel.setFocusByName("NAME");

        SECTION("a letter that is a button's quick key goes into the text")
        {
            panel.keyDown(key(SDLK_X));
            panel.textInput("x");

            REQUIRE(presses == 0);
            REQUIRE(panel.find<UiTextBox>("NAME")->get().getText() == "x");
        }

        SECTION("text reaches the focused box and no other")
        {
            panel.textInput("save");

            REQUIRE(panel.find<UiTextBox>("NAME")->get().getText() == "save");
            REQUIRE(panel.find<UiTextBox>("OTHER")->get().getText() == "");
        }

        SECTION("backspace deletes from the focused box alone")
        {
            panel.textInput("save");
            panel.setFocusByName("OTHER");
            panel.textInput("ab");
            panel.keyDown(key(SDLK_BACKSPACE));

            REQUIRE(panel.find<UiTextBox>("NAME")->get().getText() == "save");
            REQUIRE(panel.find<UiTextBox>("OTHER")->get().getText() == "a");
        }

        SECTION("a panel reports the focused box's appetite for text")
        {
            REQUIRE(panel.wantsTextInput());
            panel.setFocusByName("OK");
            REQUIRE_FALSE(panel.wantsTextInput());
        }
    }
}
