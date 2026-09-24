#include "io.h"
#include <algorithm>

namespace rwe
{
    MovementClassTdf parseMovementClassBlock(const TdfBlock& block)
    {
        MovementClassTdf m;
        m.name = block.expectString("Name");
        m.footprintX = block.expectUint("FootprintX");
        m.footprintZ = block.expectUint("FootprintZ");
        m.minWaterDepth = block.extractUint("MinWaterDepth").value_or(0);
        m.maxWaterDepth = block.extractUint("MaxWaterDepth").value_or(255);
        m.maxSlope = block.extractUint("MaxSlope").value_or(255);
        m.maxWaterSlope = block.extractUint("MaxWaterSlope").value_or(m.maxSlope);

        // The free-slope thresholds, below which a cell costs nothing extra
        // and above which, up to the max, it is "tight". The original seeds
        // each with half the corresponding max (0x4403B9, 0x4403E7) and then
        // clamps it to that max (0x440400-0x440417), so a class cannot be
        // freer than it is passable. TOTALA-EXE-MOVEMENT.md, section 95.
        m.badSlope = std::min(block.extractUint("BadSlope").value_or(m.maxSlope / 2), m.maxSlope);
        m.badWaterSlope = std::min(block.extractUint("BadWaterSlope").value_or(m.maxWaterSlope / 2), m.maxWaterSlope);

        return m;
    }

    std::vector<std::pair<std::string, MovementClassTdf>> parseMoveInfoTdf(const TdfBlock& root)
    {
        std::vector<std::pair<std::string, MovementClassTdf>> vec;
        vec.reserve(root.blocks.size());

        for (const auto& e : root.blocks)
        {
            vec.emplace_back(e.first, parseMovementClassBlock(*e.second));
        }

        return vec;
    }
}
