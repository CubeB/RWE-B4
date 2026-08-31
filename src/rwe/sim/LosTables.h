#pragma once

#include <rwe/grid/Point.h>
#include <vector>

namespace rwe
{
    /**
     * One authored line-of-sight ray, as cell offsets from the eye in step
     * order. The offsets are given for a single quadrant, so both components
     * are non-negative and neither ever decreases along the ray.
     */
    using LosRay = std::vector<Point>;

    /** The fan of rays used for one sight radius, measured in vision cells. */
    struct LosTable
    {
        std::vector<LosRay> rays;
    };

    /**
     * Total Annihilation's gamedata/los.tdf: one authored ray fan per sight
     * radius. Sight is not a stamped circle in TA - only cells that lie on a
     * ray are ever revealed, and a cell that lies on several rays gets
     * revealed by each of them.
     *
     * Each ray covers one quadrant and is replicated by the four ninety
     * degree rotations (see PlayerVisibility::revealWithLineOfSight).
     */
    struct LosTables
    {
        /**
         * Indexed by sight radius in cells. Entry 0 is empty: a unit with a
         * radius of 0 sees only the cell it is standing in.
         */
        std::vector<LosTable> tables;

        /** The largest radius that has a table. Sight radii are capped to it. */
        int maxRadius() const;

        /** The rays for a radius, which must be in 0..maxRadius(). */
        const LosTable& tableForRadius(int radius) const;
    };

    /**
     * TA's [TABLEINFO] numtables. Radii run 0..8, so terrain-mode sight never
     * reaches beyond 8 cells / 256 world units however large SightDistance is.
     */
    constexpr int DefaultLosTableCount = 9;

    /**
     * Generated ray fans for radii 1..maxRadius, shaped like TA's authored
     * ones: one ray per cell on the rim of the quadrant's disc, traced from
     * the origin with integer rounding. Used when gamedata/los.tdf cannot be
     * read from the player's data.
     */
    LosTables generateLosTables(int maxRadius);
}
