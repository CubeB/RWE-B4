#pragma once

#include <iomanip>
#include <optional>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <sstream>
#include <string>

namespace rwe
{
    std::string formatResource(const Metal& v);

    std::string formatResource(const Energy& v);

    std::string formatResourceDelta(const Metal& v);

    std::string formatResourceDelta(const Energy& v);

    /**
     * One call of the resource HUD's glide (BT-148, the block at 0x468E00):
     * the displayed amount moves toward the real one by an eighth of the
     * difference, both first truncated to integers, the division truncating
     * toward zero, and by one in the right direction when that comes to zero
     * while the difference does not. The result is held to the storage
     * capacity. The displayed amount is what the bar and the amount text show.
     */
    float hudGlideStep(float displayed, float actual, float capacity);

    /**
     * The pixels of a bar's fill. The original fills an inclusive rectangle
     * from the left edge to the left edge plus the truncated product of the
     * bar's width (right minus left, so one less than its pixel count) and
     * displayed / capacity, so a full bar of `pixelWidth` pixels fills all of
     * them and an empty one still shows its first. Nothing is drawn when the
     * capacity is not above zero.
     */
    int hudBarFillPixels(int pixelWidth, float displayed, float capacity);

    /**
     * Where the share marker's left edge sits, in pixels from the bar's left
     * edge, or nothing: it is drawn only when the share threshold is above
     * zero and the real amount is strictly above it. It is hudShareMarkerWidth
     * pixels wide (two further right, inclusive) over the bar's full height.
     */
    std::optional<int> hudShareMarkerOffset(int pixelWidth, float threshold, float capacity, float actual);

    constexpr int hudShareMarkerWidth = 3;

    /**
     * Energy income prints as a truncated integer, or as that value divided
     * by 1000 followed by K when it is above 99999. Energy use prints as the
     * truncated absolute value, and its K form applies only below minus 99999,
     * so a positive use of six digits prints in full -- a quirk of the
     * original, whose test is on the negative threshold. Metal has no K rule.
     */
    std::string formatHudEnergyIncome(const Energy& v);

    std::string formatHudEnergyUse(const Energy& v);
}
