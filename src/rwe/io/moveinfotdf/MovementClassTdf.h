#pragma once

#include <string>

namespace rwe
{
    struct MovementClassTdf
    {
        std::string name;
        unsigned int footprintX;
        unsigned int footprintZ;
        unsigned int minWaterDepth;
        unsigned int maxWaterDepth;

        /** Whether the class wrote MinWaterDepth at all (BT-109). */
        bool hasMinWaterDepth{false};
        unsigned int maxSlope;
        unsigned int maxWaterSlope;
        unsigned int badSlope;
        unsigned int badWaterSlope;
    };
}
