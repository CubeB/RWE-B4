#pragma once

#include <rwe/sim/SimVector.h>

namespace rwe
{
    struct UnitPath
    {
        std::vector<SimVector> waypoints;

        /**
         * True when the requested destination cannot be reached at all;
         * the path then ends at the closest point that can be.
         */
        bool destinationUnreachable{false};
    };
}
