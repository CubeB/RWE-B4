#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include <memory>
#include <rwe/ui/UiIdleBuilderSign.h>
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

    // Clicking a gadget is what gives it the focus, and the side panel's
    // idle-builder sign is a gadget a player clicks -- with the pointer, not
    // with a key. UiPanel::mouseDown sets the focus on the child under the
    // pointer before handing it the event, and mouseUp then goes to whichever
    // child holds it rather than to the one under the pointer, so a press that
    // has wandered off the panel between the two halves still lands.
    TEST_CASE("a clicked gadget takes the focus, and the release finds it there", "[ui]")
    {
        auto activations = 0;

        UiPanel panel(0, 0, 128, 416);
        {
            auto sign = std::make_unique<UiIdleBuilderSign>(0, 0, 120, 14, nullptr);
            sign->setName("IDLEBUILDERS");
            sign->setIdleBuilderCount(2);
            sign->addSubscription(sign->messages().subscribe([&activations](const ControlMessage& message) {
                if (std::holds_alternative<ActivateMessage>(message))
                {
                    ++activations;
                }
            }));
            panel.appendChild(std::move(sign));
        }

        // The sign sits at the panel's own origin, and the panel translates the
        // event on the way in, so these are the sign's own coordinates.
        panel.mouseDown(MouseButtonEvent(10, 5, MouseButtonEvent::MouseButton::Left));

        SECTION("a release somewhere else entirely still reaches it")
        {
            panel.mouseUp(MouseButtonEvent(400, 300, MouseButtonEvent::MouseButton::Left));

            REQUIRE(activations == 1);
        }

        SECTION("a release with no press on the panel reaches nothing")
        {
            // The click took the focus, so this is the one that matters: a
            // release on its own is not a click, and the sign's armed flag is
            // what says so.
            panel.clearFocus();
            panel.setFocusByName("IDLEBUILDERS");
            panel.mouseUp(MouseButtonEvent(400, 300, MouseButtonEvent::MouseButton::Left));

            REQUIRE(activations == 0);
        }

        SECTION("a press that lands on no gadget at all reaches nothing")
        {
            panel.clearFocus();
            panel.mouseDown(MouseButtonEvent(400, 300, MouseButtonEvent::MouseButton::Left));
            panel.mouseUp(MouseButtonEvent(10, 5, MouseButtonEvent::MouseButton::Left));

            REQUIRE(activations == 0);
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
