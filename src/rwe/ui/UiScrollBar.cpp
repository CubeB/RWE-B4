#include "UiScrollBar.h"
#include <algorithm>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    void drawSpriteAt(UiRenderService& graphics, float x, float y, const Sprite& sprite)
    {
        graphics.drawSpriteAbs(x, y, sprite);
    }

    bool UiScrollBar::hasUsableArt(const SpriteSeries* series)
    {
        if (!series || series->sprites.size() < 10)
        {
            return false;
        }
        for (std::size_t i = 0; i < 10; ++i)
        {
            if (!series->sprites[i])
            {
                return false;
            }
        }
        // The middle background (1) and middle box (4) are tiled by loops that
        // advance by their height.
        return series->sprites[1]->bounds.height() > 0.0f && series->sprites[4]->bounds.height() > 0.0f;
    }

    void UiScrollBar::render(UiRenderService& context) const
    {
        if (!artUsable)
        {
            return;
        }

        drawScrollBackground(context, posX, posY, sizeY);

        auto info = getScrollBoxInfo();

        drawScrollBox(context, posX + 3.0f, posY + std::round(info.pos), std::round(info.size));
    }

    UiScrollBar::UiScrollBar(
        int posX,
        int posY,
        unsigned int sizeX,
        unsigned int sizeY,
        std::shared_ptr<SpriteSeries> sprites)
        : UiComponent(posX, posY, sizeX, sizeY),
          sprites(std::move(sprites))
    {
        artUsable = hasUsableArt(this->sprites.get());
        if (!artUsable)
        {
            LOG_WARN << "scroll bar: the SLIDERS art is incomplete; the bar is blank";
        }

        scrollChanged().subscribe([this](float scrollPercent) {
            ScrollPositionMessage m{getScrollBarPercent(), scrollPercent};
            messagesSubject.next(m);
        });

        scrollUp().subscribe([this](const auto& /*msg*/) {
            messagesSubject.next(ScrollUpMessage());
        });

        scrollDown().subscribe([this](const auto& /*msg*/) {
            messagesSubject.next(ScrollDownMessage());
        });
    }

    void UiScrollBar::drawScrollBackground(UiRenderService& graphics, float x, float y, float height) const
    {
        const Sprite& topBackground = *sprites->sprites[0];
        const Sprite& middleBackground = *sprites->sprites[1];
        const Sprite& bottomBackground = *sprites->sprites[2];

        const Sprite& upArrow = *sprites->sprites[upArrowPressed ? 7 : 6];
        const Sprite& downArrow = *sprites->sprites[downArrowPressed ? 9 : 8];

        float bottomMargin = bottomBackground.bounds.height() + downArrow.bounds.height();

        float yOffset = 0.0f;

        drawSpriteAt(graphics, x, y + yOffset, upArrow);
        yOffset += upArrow.bounds.height();

        drawSpriteAt(graphics, x, y + yOffset, topBackground);
        yOffset += topBackground.bounds.height();


        while (yOffset < height - bottomMargin)
        {
            drawSpriteAt(graphics, x, y + yOffset, middleBackground);
            yOffset += middleBackground.bounds.height();
        }

        drawSpriteAt(graphics, x, y + height - bottomMargin, bottomBackground);
        bottomMargin -= bottomBackground.bounds.height();

        drawSpriteAt(graphics, x, y + height - bottomMargin, downArrow);
    }

    void UiScrollBar::drawScrollBox(UiRenderService& context, float x, float y, float height) const
    {
        const Sprite& topBox = *sprites->sprites[3];
        const Sprite& middleBox = *sprites->sprites[4];
        const Sprite& bottomBox = *sprites->sprites[5];

        drawSpriteAt(context, x, y, topBox);

        float bottomBoxStart = height - bottomBox.bounds.height();

        float yOffset = topBox.bounds.height();
        while (yOffset + middleBox.bounds.height() <= bottomBoxStart)
        {
            drawSpriteAt(context, x, y + yOffset, middleBox);
            yOffset += middleBox.bounds.height();
        }

        drawSpriteAt(context, x, y + yOffset, bottomBox);
    }

    void UiScrollBar::mouseDown(MouseButtonEvent event)
    {
        if (!artUsable)
        {
            return;
        }

        const Sprite& upArrow = *sprites->sprites[6];
        if (Rectangle2f::fromTopLeft(posX, posY, upArrow.bounds.width(), upArrow.bounds.height()).contains(event.x, event.y))
        {
            upArrowPressed = true;
            scrollUpSubject.next(true);
            return;
        }

        const Sprite& downArrow = *sprites->sprites[8];
        if (Rectangle2f::fromTopLeft(posX, posY + sizeY - downArrow.bounds.height(), downArrow.bounds.width(), downArrow.bounds.height()).contains(event.x, event.y))
        {
            downArrowPressed = true;
            scrollDownSubject.next(true);
            return;
        }

        auto boxInfo = getScrollBoxInfo();
        if (Rectangle2f::fromTopLeft(posX, posY + boxInfo.pos, sizeX, boxInfo.size).contains(event.x, event.y))
        {
            barGrabbed = true;
            mouseDownY = event.y;
            mouseDownScrollPercent = scrollPercent;
            return;
        }

        backgroundPressed = true;
        mouseY = event.y;
    }

    void UiScrollBar::mouseUp(MouseButtonEvent /*event*/)
    {
        upArrowPressed = false;
        downArrowPressed = false;
        barGrabbed = false;
        backgroundPressed = false;
    }

    void UiScrollBar::mouseMove(MouseMoveEvent event)
    {
        UiComponent::mouseMove(event);

        if (!artUsable)
        {
            return;
        }

        if (barGrabbed)
        {
            auto deltaPixels = static_cast<float>(event.y - mouseDownY);

            auto boxInfo = getScrollBoxInfo();

            // BT-MODEL-HEIGHT-SCROLLBAR-FINDINGS: no travel means no drag
            // update. A percentage is undefined here (0/0 when stationary).
            if (boxInfo.range <= 0.0f)
            {
                return;
            }

            float deltaPercent = deltaPixels / boxInfo.range;
            scrollChangedSubject.next(std::clamp(mouseDownScrollPercent + deltaPercent, 0.0f, 1.0f));
        }

        if (backgroundPressed)
        {
            mouseY = event.y;
        }
    }

    UiScrollBar::ScrollBoxInfo UiScrollBar::getScrollBoxInfo() const
    {
        const Sprite& upArrow = *sprites->sprites[6];
        const Sprite& downArrow = *sprites->sprites[8];

        float topMargin = upArrow.bounds.height() + 3.0f;
        float bottomMargin = downArrow.bounds.height() + 3.0f;

        float boxRange = sizeY - topMargin - bottomMargin;
        float boxSize = getEffectiveScrollBarPercent() * boxRange;
        float boxTopRange = boxRange - boxSize;

        float boxY = topMargin + (boxTopRange * scrollPercent);

        return ScrollBoxInfo{boxY, boxSize, boxTopRange};
    }

    float UiScrollBar::toScrollPercent(float pos)
    {
        const Sprite& upArrow = *sprites->sprites[6];
        const Sprite& downArrow = *sprites->sprites[8];

        float topMargin = upArrow.bounds.height() + 3.0f;
        float bottomMargin = downArrow.bounds.height() + 3.0f;

        float boxRange = sizeY - topMargin - bottomMargin;

        return (pos - topMargin) / boxRange;
    }

    void UiScrollBar::update(float dt)
    {
        if (!artUsable)
        {
            return;
        }

        // speed in percent/sec
        float scrollSpeed = 4.0f;

        if (backgroundPressed)
        {
            auto cursorPercent = toScrollPercent(mouseY - posY);

            auto boxTopPercent = scrollPercent * (1.0f - getEffectiveScrollBarPercent());

            if (cursorPercent < boxTopPercent) // cursor above
            {
                scrollChangedSubject.next(std::clamp(scrollPercent - (scrollSpeed * dt), 0.0f, 1.0f));
            }
            else if (cursorPercent > boxTopPercent + getEffectiveScrollBarPercent()) // cursor below
            {
                scrollChangedSubject.next(std::clamp(scrollPercent + (scrollSpeed * dt), 0.0f, 1.0f));
            }

            return;
        }
    }

    void UiScrollBar::setScrollPercent(float percent)
    {
        scrollPercent = percent;
    }

    void UiScrollBar::setScrollBarPercent(float percent)
    {
        scrollBarPercent = percent;
    }

    void UiScrollBar::uiMessage(const GroupMessage& message)
    {
        if (message.controlName == name)
        {
            return;
        }

        if (message.group != group)
        {
            return;
        }

        auto scrollMessage = std::get_if<ScrollPositionMessage>(&(message.message));
        if (!scrollMessage)
        {
            return;
        }

        scrollPercent = scrollMessage->scrollPosition;
        scrollBarPercent = scrollMessage->scrollViewportPercent;
    }

    Observable<float>& UiScrollBar::scrollChanged()
    {
        return scrollChangedSubject;
    }

    const Observable<float>& UiScrollBar::scrollChanged() const
    {
        return scrollChangedSubject;
    }

    float UiScrollBar::getScrollBarPercent() const
    {
        return scrollBarPercent;
    }

    Observable<bool>& UiScrollBar::scrollUp()
    {
        return scrollUpSubject;
    }

    const Observable<bool>& UiScrollBar::scrollUp() const
    {
        return scrollUpSubject;
    }

    Observable<bool>& UiScrollBar::scrollDown()
    {
        return scrollDownSubject;
    }

    const Observable<bool>& UiScrollBar::scrollDown() const
    {
        return scrollDownSubject;
    }

    void UiScrollBar::mouseWheel(MouseWheelEvent event)
    {
        if (event.y > 0)
        {
            scrollUpSubject.next(true);
        }
        else if (event.y < 0)
        {
            scrollDownSubject.next(true);
        }
    }

    float UiScrollBar::getEffectiveScrollBarPercent() const
    {
        auto minBarPixels = 14.0f;
        auto minBarPercent = minBarPixels / static_cast<float>(sizeY);
        return std::max(scrollBarPercent, minBarPercent);
    }
}
