#include "resource_io.h"
#include <algorithm>

namespace rwe
{
    std::string formatResource(const Metal& v)
    {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(0) << v.value;
        return ss.str();
    }

    std::string formatResource(const Energy& v)
    {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(0) << v.value;
        return ss.str();
    }

    std::string formatResourceDelta(const Metal& v)
    {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << v.value;
        return ss.str();
    }

    std::string formatResourceDelta(const Energy& v)
    {
        return formatResource(v);
    }

    float hudGlideStep(float displayed, float actual, float capacity)
    {
        auto from = static_cast<int>(displayed);
        auto to = static_cast<int>(actual);
        auto difference = to - from;
        auto step = difference / 8;
        if (step == 0 && difference != 0)
        {
            step = difference > 0 ? 1 : -1;
        }
        auto next = static_cast<float>(from + step);
        return next > capacity ? capacity : next;
    }

    int hudBarFillPixels(int pixelWidth, float displayed, float capacity)
    {
        if (!(capacity > 0.0f) || pixelWidth <= 0)
        {
            return 0;
        }
        auto barWidth = static_cast<float>(pixelWidth - 1);
        auto reach = static_cast<int>(barWidth * displayed / capacity);
        return std::clamp(reach, 0, pixelWidth - 1) + 1;
    }

    std::optional<int> hudShareMarkerOffset(int pixelWidth, float threshold, float capacity, float actual)
    {
        if (!(threshold > 0.0f) || !(actual > threshold) || !(capacity > 0.0f) || pixelWidth <= 0)
        {
            return std::nullopt;
        }
        auto barWidth = static_cast<float>(pixelWidth - 1);
        return std::clamp(static_cast<int>(barWidth * threshold / capacity), 0, pixelWidth - 1);
    }

    std::string formatHudEnergyIncome(const Energy& v)
    {
        if (v.value > 99999.0f)
        {
            return std::to_string(static_cast<int>(v.value) / 1000) + "K";
        }
        return std::to_string(static_cast<int>(v.value));
    }

    std::string formatHudEnergyUse(const Energy& v)
    {
        if (v.value < -99999.0f)
        {
            return std::to_string(static_cast<int>(-v.value) / 1000) + "K";
        }
        return std::to_string(static_cast<int>(v.value < 0.0f ? -v.value : v.value));
    }
}
