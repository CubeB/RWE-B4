#include "panel_slide.h"

#include <algorithm>

namespace rwe
{
    bool panelWantsHiding(bool f4Latch, bool spaceHeld, bool cursorOnPanel)
    {
        if (f4Latch)
        {
            return true;
        }

        return spaceHeld && !cursorOnPanel;
    }

    float advancePanelSlide(float current, float target, float pixelsPerSecond, int millisecondsElapsed)
    {
        auto step = pixelsPerSecond * (static_cast<float>(millisecondsElapsed) / 1000.0f);
        if (current < target)
        {
            return std::min(target, current + step);
        }
        if (current > target)
        {
            return std::max(target, current - step);
        }
        return current;
    }
}
