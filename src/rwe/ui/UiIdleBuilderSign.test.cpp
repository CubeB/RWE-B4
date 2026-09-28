#include <catch2/catch_test_macros.hpp>
#include <rwe/ui/UiIdleBuilderSign.h>
#include <rwe/ui/UiPanel.h>
#include <rwe/ui/events.h>
#include <memory>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * A sign with nothing to draw into. `render` is the one thing here that
         * cannot be exercised without a graphics context, and nothing below
         * calls it.
         */
        std::unique_ptr<UiIdleBuilderSign> makeSign(int idleBuilderCount = 2)
        {
            auto sign = std::make_unique<UiIdleBuilderSign>(0, 0, 120, 14, nullptr);
            sign->setName("IDLEBUILDERS");
            sign->setIdleBuilderCount(idleBuilderCount);
            return sign;
        }

        void countActivations(UiComponent& component, int& count)
        {
            component.addSubscription(component.messages().subscribe([&count](const ControlMessage& message) {
                if (std::holds_alternative<ActivateMessage>(message))
                {
                    ++count;
                }
            }));
        }

        MouseButtonEvent leftClick(int x, int y)
        {
            return MouseButtonEvent(x, y, MouseButtonEvent::MouseButton::Left);
        }
    }

    // The sign is the first thing on the HUD that reads the simulation every
    // frame, and the two things it must never do are write to it and hold a
    // subscription it does not hand back. It holds no reference to the
    // simulation at all -- the count is pushed in -- so there is nothing for it
    // to write through, and it subscribes to nothing, so there is nothing for it
    // to leak. What is left to pin is the click, and the lifetime of the panel
    // that owns it.
    TEST_CASE("the idle-builder sign reports a click and nothing else", "[ui]")
    {
        auto sign = makeSign();
        auto clicks = 0;
        countActivations(*sign, clicks);

        SECTION("a press and a release on the sign is one activation")
        {
            sign->mouseDown(leftClick(10, 5));
            sign->mouseUp(leftClick(10, 5));

            REQUIRE(clicks == 1);
        }

        SECTION("a release with no press on it is not an activation")
        {
            // The panel routes mouseUp to whichever child has the focus rather
            // than to the one under the pointer, so the armed flag is the whole
            // of the hit test on the way up. This is the drag that starts on
            // the sign and ends somewhere else.
            sign->mouseUp(leftClick(10, 5));

            REQUIRE(clicks == 0);
        }

        SECTION("the right button is not the sign's")
        {
            // There is no second action behind the sign, so arming on a right
            // click would leave it looking held for as long as the button was.
            sign->mouseDown(MouseButtonEvent(10, 5, MouseButtonEvent::MouseButton::Right));
            sign->mouseUp(MouseButtonEvent(10, 5, MouseButtonEvent::MouseButton::Right));

            REQUIRE(clicks == 0);
        }

        SECTION("a sign with nothing to jump to does not answer")
        {
            sign->setIdleBuilderCount(0);
            sign->mouseDown(leftClick(10, 5));
            sign->mouseUp(leftClick(10, 5));

            REQUIRE(clicks == 0);
        }

        SECTION("the last builder picking up work mid-press cancels the click")
        {
            // The count is pushed in once a frame, so this is an ordinary
            // ordering rather than a race: the press went down on a live sign
            // and the release arrived after the sign went quiet.
            sign->mouseDown(leftClick(10, 5));
            sign->setIdleBuilderCount(0);
            sign->mouseUp(leftClick(10, 5));

            REQUIRE(clicks == 0);
        }

        SECTION("losing the focus drops a press that was still down")
        {
            sign->mouseDown(leftClick(10, 5));
            sign->unfocus();
            sign->mouseUp(leftClick(10, 5));

            REQUIRE(clicks == 0);
        }

        SECTION("the count is what it was handed, and never below zero")
        {
            REQUIRE(sign->getIdleBuilderCount() == 2);
            sign->setIdleBuilderCount(7);
            REQUIRE(sign->getIdleBuilderCount() == 7);
            sign->setIdleBuilderCount(-3);
            REQUIRE(sign->getIdleBuilderCount() == 0);
        }
    }

    // This is the whole wiring: the sign is a child of the side panel, and its
    // activation leaves as a group message under its own name. That is the route
    // every gadget out of a gui file already takes, so the scene needed no
    // subscription of its own and cannot be left holding one to a widget that a
    // panel rebuild has destroyed.
    TEST_CASE("a click on the sign comes out of the panel under the sign's name", "[ui]")
    {
        UiPanel panel(0, 0, 128, 416);
        std::vector<std::string> activated;
        panel.groupMessages().subscribe([&activated](const GroupMessage& message) {
            if (std::holds_alternative<ActivateMessage>(message.message))
            {
                activated.push_back(message.controlName);
            }
        });

        auto sign = makeSign();
        sign->setName("IDLEBUILDERS");
        auto signX = sign->getX();
        auto signY = sign->getY();
        panel.appendChild(std::move(sign));

        // And the other half of the wiring: the scene finds the sign on the
        // panel by this name once a frame to hand it the count, and a
        // scenario's clickGadget finds it the same way. A name that did not
        // take would leave a widget on screen that never updated.
        auto found = panel.find<UiIdleBuilderSign>("IDLEBUILDERS");
        REQUIRE(found.has_value());
        REQUIRE(found->get().getIdleBuilderCount() == 2);
        REQUIRE_FALSE(panel.find<UiIdleBuilderSign>("SOMETHINGELSE").has_value());

        panel.mouseDown(leftClick(signX + 10, signY + 5));
        panel.mouseUp(leftClick(signX + 10, signY + 5));

        REQUIRE(activated == std::vector<std::string>{"IDLEBUILDERS"});
    }

    // A panel rebuild destroys the sign that was on the old panel, and the sign
    // is a child of it, so the panel is the subscriber to the sign's subject and
    // is necessarily destroyed after it. UiPanel::appendChild throws the
    // subscription handle away on purpose, which is only sound because of that
    // order. Nothing here can observe a violation of it -- it is undefined
    // behaviour, and on a good day undefined behaviour does nothing -- so this
    // walks the path for a sanitizer run to have something to catch, the same
    // reason UiComponent.test.cpp does.
    TEST_CASE("a panel holding the sign can be destroyed with the sign focused", "[ui]")
    {
        auto panel = std::make_unique<UiPanel>(0, 0, 128, 416);
        auto sign = makeSign();
        auto signX = sign->getX();
        auto signY = sign->getY();
        panel->appendChild(std::move(sign));
        panel->groupMessages().subscribe([](const GroupMessage&) {});

        // Click it, so the panel really does hold a focused child at the moment
        // it dies rather than an empty optional.
        panel->mouseDown(leftClick(signX + 10, signY + 5));
        panel->mouseUp(leftClick(signX + 10, signY + 5));

        panel.reset();
    }
}
