#include <catch2/catch_test_macros.hpp>
#include <rwe/ui/UiScrollBar.h>
#include <cmath>

namespace rwe
{
    namespace
    {
        // Valid untextured art exercises input without creating a GL context.
        // The regression depends on available travel, not on the bitmap pixels.
        std::shared_ptr<SpriteSeries> scrollBarArt()
        {
            auto art = std::make_shared<SpriteSeries>();
            for (int i = 0; i < 10; ++i)
            {
                art->sprites.push_back(std::make_shared<Sprite>(
                    Rectangle2f::fromTopLeft(0, 0, 8, 8), SharedTextureHandle(), nullptr));
            }
            return art;
        }

        class InspectableScrollBar final : public UiScrollBar
        {
        public:
            using UiScrollBar::UiScrollBar;
            using UiScrollBar::getScrollPercent;
        };
    }

    TEST_CASE("dragging a scroll thumb without positive travel emits no position", "[ui][scrollbar]")
    {
        // #669: the stationary move used to send NaN through the group message.
        // An oversized thumb is defensive RWE handling, not original parity.
        for (float viewport : {1.0f, 1.5f})
        {
            CAPTURE(viewport);
            unsigned int updates = 0;
            unsigned int messages = 0;
            InspectableScrollBar bar(0, 0, 16, 100, scrollBarArt());
            bar.setScrollBarPercent(viewport);
            bar.setScrollPercent(0.4f);
            bar.addSubscription(bar.scrollChanged().subscribe([&](float) { ++updates; }));
            bar.addSubscription(bar.messages().subscribe([&](const ControlMessage&) { ++messages; }));
            bar.mouseDown(MouseButtonEvent(8, 50, MouseButtonEvent::MouseButton::Left));

            for (auto y : {50, 90, 10})
            {
                bar.mouseMove(MouseMoveEvent(8, y));
                REQUIRE(updates == 0);
                REQUIRE(messages == 0);
                REQUIRE(bar.getScrollPercent() == 0.4f);
                REQUIRE(std::isfinite(bar.getScrollPercent()));
            }
            bar.mouseUp(MouseButtonEvent(8, 10, MouseButtonEvent::MouseButton::Left));
        }
    }

    TEST_CASE("a scroll thumb with positive travel still moves and clamps", "[ui][scrollbar]")
    {
        std::vector<float> positions;
        InspectableScrollBar bar(0, 0, 16, 100, scrollBarArt());
        bar.setScrollBarPercent(0.5f);
        bar.addSubscription(bar.scrollChanged().subscribe([&](float position) { positions.push_back(position); }));
        bar.mouseDown(MouseButtonEvent(8, 20, MouseButtonEvent::MouseButton::Left));
        bar.mouseMove(MouseMoveEvent(8, 30));
        bar.mouseMove(MouseMoveEvent(8, 200));
        bar.mouseMove(MouseMoveEvent(8, -100));
        REQUIRE(positions.size() == 3);
        REQUIRE(std::isfinite(positions[0]));
        REQUIRE(positions[0] > 0.0f);
        REQUIRE(positions[0] < 1.0f);
        REQUIRE(positions[1] == 1.0f);
        REQUIRE(positions[2] == 0.0f);
    }
}
