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

        /**
         * Whether the effective record names a MinWaterDepth at all (BT-109).
         * The original keeps the field as a signed word whose default is
         * -10000 (0x4402F6), so an absent key is negative and a written one
         * is not; the pathfinder and the placement checks use the unsigned
         * `minWaterDepth` above, where absent reads as zero, and only the
         * classic computer player asks the sign. A movement class wins over
         * the FBI's own key (0x42CD3B), which is what RWE's choice of class
         * or ad-hoc record already does.
         */
        bool minWaterDepthPresent{false};

        /** The signed word at definition +0x1C0: the depth if written, else -10000. */
        int signedMinWaterDepth() const
        {
            return minWaterDepthPresent ? static_cast<int>(minWaterDepth) : -10000;
        }
    };
}
