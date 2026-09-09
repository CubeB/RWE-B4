#include <catch2/catch_test_macros.hpp>
#include <rwe/ui/UiListBox.h>
#include <rwe/ui/UiStagedButton.h>

namespace rwe
{
    namespace
    {
        std::unique_ptr<UiStagedButton> makeButton()
        {
            // One stage, with no art in it. The button refuses to be built
            // with none at all, and nothing here draws.
            std::vector<UiStagedButton::StageInfo> stages;
            stages.emplace_back(nullptr, std::string());

            return std::make_unique<UiStagedButton>(
                0, 0, 10, 10, std::move(stages), nullptr, nullptr);
        }
    }

    // These pin the destruction order, not a behaviour anyone can see on
    // screen. A component's subscription store lives in UiComponent, the
    // base, and a base destructor runs *after* the derived class's own
    // members are gone. So a subscription to a subject the derived class owns
    // -- UiStagedButton::hoverSubject, UiListBox::selectedIndexSubject -- was
    // being handed back to a Subject that no longer existed, and
    // Subject::unsubscribe walks and erases a vector that has been destroyed.
    // It cost a crash on entering a skirmish: the main menu's option buttons
    // carry exactly such a subscription and its panels are destroyed on the
    // way into the game. Each of these classes releases in its own destructor
    // now, while its subjects are still alive.
    //
    // Nothing here can *observe* the old fault -- it is undefined behaviour,
    // and on a good day undefined behaviour does nothing at all. What these
    // cases do is walk the exact path under whatever the build is checked
    // with, so a sanitizer run has something to catch.
    TEST_CASE("a component may subscribe to its own subject and be destroyed", "[ui]")
    {
        SECTION("a staged button, hover")
        {
            auto button = makeButton();
            auto fired = false;
            button->addSubscription(button->onHover().subscribe([&fired](bool) { fired = true; }));

            // Live before the destruction, so the subscription is real rather
            // than something the compiler was free to fold away.
            button->mouseEnter();
            REQUIRE(fired);

            button.reset();
        }

        SECTION("a staged button, clicks")
        {
            auto button = makeButton();
            button->addSubscription(button->onClick().subscribe([](const ButtonClickEvent&) {}));
            button.reset();
        }

        SECTION("a list box, selection")
        {
            auto listBox = std::make_unique<UiListBox>(0, 0, 10, 10, nullptr);
            listBox->appendItem("one");
            listBox->appendItem("two");

            auto seen = std::optional<unsigned int>();
            listBox->addSubscription(listBox->selectedIndex().subscribe(
                [&seen](const std::optional<unsigned int>& i) { seen = i; }));

            listBox->setSelectedItem("two");
            REQUIRE(seen == std::optional<unsigned int>(1));

            listBox.reset();
        }
    }

    TEST_CASE("releaseSubscriptions is idempotent", "[ui]")
    {
        // ~UiComponent calls it too, so a derived destructor that has already
        // called it must leave nothing behind for the second pass -- which is
        // the whole point, that second pass being the one that would run with
        // the derived subjects gone.
        auto button = makeButton();
        button->addSubscription(button->onHover().subscribe([](bool) {}));

        button->releaseSubscriptions();
        button->releaseSubscriptions();

        button.reset();
    }
}
