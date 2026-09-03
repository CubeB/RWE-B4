#include "UiSlider.h"

#include <algorithm>

namespace rwe
{
    UiSlider::UiSlider(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::shared_ptr<SpriteSeries> sprites)
        : UiScrollBar(posX, posY, sizeX, sizeY, std::move(sprites))
    {
    }

    void UiSlider::render(UiRenderService& context) const
    {
        // A groove with a knob at the current value. Drawn with plain fills:
        // the SLIDERS artwork is a vertical scrollbar's, and rotating it
        // would look worse than an honest slider.
        auto y = static_cast<float>(posY);
        auto x = static_cast<float>(posX);
        auto width = static_cast<float>(sizeX);
        auto height = static_cast<float>(sizeY);

        auto grooveY = y + (height / 2.0f) - 2.0f;
        context.fillColor(x, grooveY, width, 4.0f, Color(20, 24, 20));
        context.drawBoxOutline(x, grooveY, width, 4.0f, Color(90, 100, 90), 1.0f);

        const float knobWidth = 9.0f;
        auto knobX = x + ((width - knobWidth) * std::clamp(getScrollPercent(), 0.0f, 1.0f));
        context.fillColor(knobX, y + 1.0f, knobWidth, height - 2.0f, Color(120, 132, 120));
        context.drawBoxOutline(knobX, y + 1.0f, knobWidth, height - 2.0f, Color(200, 210, 200), 1.0f);
    }

    void UiSlider::setFromMouse(int mouseX)
    {
        const float knobWidth = 9.0f;
        auto usable = static_cast<float>(sizeX) - knobWidth;
        if (usable <= 0.0f)
        {
            return;
        }
        auto percent = (static_cast<float>(mouseX - posX) - (knobWidth / 2.0f)) / usable;
        notifyScrollChanged(std::clamp(percent, 0.0f, 1.0f));
    }

    void UiSlider::mouseDown(MouseButtonEvent event)
    {
        grabForDrag();
        setFromMouse(event.x);
    }

    void UiSlider::mouseUp(MouseButtonEvent /*event*/)
    {
        releaseDrag();
    }

    void UiSlider::mouseMove(MouseMoveEvent event)
    {
        UiComponent::mouseMove(event);
        if (isDragging())
        {
            setFromMouse(event.x);
        }
    }

    void UiSlider::mouseWheel(MouseWheelEvent event)
    {
        auto step = event.y > 0 ? 0.05f : -0.05f;
        notifyScrollChanged(std::clamp(getScrollPercent() + step, 0.0f, 1.0f));
    }
}
