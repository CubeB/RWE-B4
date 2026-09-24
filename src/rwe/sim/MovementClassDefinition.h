#pragma once

#include <string>

namespace rwe
{
    struct MovementClassDefinition
    {
        std::string name;
        unsigned int footprintX;
        unsigned int footprintZ;
        unsigned int minWaterDepth;
        unsigned int maxWaterDepth;
        unsigned int maxSlope;
        unsigned int maxWaterSlope;

        /**
         * The slope up to which a dry cell costs nothing to cross. Between
         * this and maxSlope the cell is passable but "tight", and the
         * pathfinder charges extra for it. Half of maxSlope unless the
         * movement class says otherwise (BadSlope); the hover classes set it
         * equal to their maxSlope, so they pay nothing for ground a tank
         * calls rough.
         */
        unsigned int badSlope{0};

        /** The same for a cell under water, against maxWaterSlope (BadWaterSlope). */
        unsigned int badWaterSlope{0};
    };
}
