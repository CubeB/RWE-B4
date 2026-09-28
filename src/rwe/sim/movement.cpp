#include "movement.h"

namespace rwe
{
    bool
    isGridPointWalkable(const MapTerrain& terrain, const MovementClassDefinition& movementClass, unsigned int x, unsigned int y)
    {
        if (isMaxSlopeGreaterThan(terrain, x, y, movementClass.footprintX, movementClass.footprintZ, movementClass.maxSlope, movementClass.maxWaterSlope))
        {
            return false;
        }

        if (!isWaterDepthWithinBounds(terrain, x, y, movementClass.footprintX, movementClass.footprintZ, movementClass.minWaterDepth, movementClass.maxWaterDepth))
        {
            return false;
        }

        return true;
    }

    /**
     * Whether any cell of the footprint starting at (x,y) is steeper than the
     * limit that applies to it -- the wet one if the footprint is under water,
     * the dry one if it is not.
     *
     * The per-cell slope comes out of MapTerrain's cache, which is getSlope's
     * arithmetic done once per cell when the map was loaded rather than once
     * per cell per question. That is the whole of the difference, and the
     * rest is deliberately untouched: the same cells in the same order, the
     * same comparison against the same limit, and the same early exit, so a
     * footprint refused on its first steep cell is still refused on its first
     * steep cell. movement.test.cpp walks a map holding every cached slope
     * against getSlope cell by cell, which is what makes that safe rather
     * than merely intended.
     *
     * Which limit applies is still isAreaUnderWater's question, asked of the
     * heightmap over the same (w+1)x(h+1) block it has always walked. Being
     * below sea level is a fact about a cell's height rather than about the
     * range of a 2x2 block, so the slope grid has nothing to answer it with.
     */
    bool isMaxSlopeGreaterThan(const MapTerrain& terrain, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int maxSlope, unsigned int maxWaterSlope)
    {
        const auto& heights = terrain.getHeightMap();
        const auto waterLevel = simScalarToUInt(terrain.getSeaLevel());
        auto isUnderWater = isAreaUnderWater(heights, waterLevel, x, y, width, height);
        auto effectiveMaxSlope = isUnderWater ? maxWaterSlope : maxSlope;

        const auto& slopes = terrain.getSlopeMap();
        for (unsigned int dy = 0; dy < height; ++dy)
        {
            for (unsigned int dx = 0; dx < width; ++dx)
            {
                if (slopes.get(x + dx, y + dy) > effectiveMaxSlope)
                {
                    return true;
                }
            }
        }

        return false;
    }

    /**
     * Whether every cell of the footprint starting at (x,y) is between
     * minWaterDepth and maxWaterDepth deep.
     *
     * As above: the same walk, the same order, the same two comparisons, with
     * the depth read out of MapTerrain's cache rather than subtracted from
     * the sea level each time. MapTerrain::getWaterDepthAt falls back to the
     * heightmap when the sea level is above 255 and a depth would not fit a
     * byte, so this is getWaterDepth's arithmetic either way.
     */
    bool
    isWaterDepthWithinBounds(const MapTerrain& terrain, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int minWaterDepth, unsigned int maxWaterDepth)
    {
        for (unsigned int dy = 0; dy < height; ++dy)
        {
            for (unsigned int dx = 0; dx < width; ++dx)
            {
                auto cellWaterDepth = terrain.getWaterDepthAt(static_cast<int>(x + dx), static_cast<int>(y + dy));
                if (cellWaterDepth < minWaterDepth)
                {
                    return false;
                }

                if (cellWaterDepth > maxWaterDepth)
                {
                    return false;
                }
            }
        }

        return true;
    }

    /**
     * The water depth of one cell, read off the heightmap.
     *
     * The definition MapTerrain's cached depth grid holds, and what
     * MapTerrain::getWaterDepthAt falls back to when there is no grid to
     * read. Kept as a function rather than open-coded in both so the two
     * cannot drift.
     */
    unsigned int
    getWaterDepth(const Grid<unsigned char>& heights, unsigned int waterLevel, unsigned int x, unsigned int y)
    {
        auto height = heights.get(x, y);
        return height < waterLevel ? waterLevel - height : 0;
    }

    /**
     * The geometric slope of a cell: the height range over the 2x2 block of
     * corners at (x,y)..(x+1,y+1).
     *
     * The definition MapTerrain's cached slope grid holds. The pathfinder
     * asks the same question of a cell it is standing beside rather than of
     * a whole footprint, and memoises the answer per cell for the length of
     * a search, so there is no repeated walk for a cache to amortise and it
     * is left on this.
     */
    unsigned int getSlope(const Grid<unsigned char>& heights, unsigned int x, unsigned int y)
    {
        unsigned int minHeight = 255;
        unsigned int maxHeight = 0;

        for (unsigned int dy = 0; dy < 2; ++dy)
        {
            for (unsigned int dx = 0; dx < 2; ++dx)
            {
                auto cellHeight = heights.get(x + dx, y + dy);

                if (cellHeight < minHeight)
                {
                    minHeight = cellHeight;
                }

                if (cellHeight > maxHeight)
                {
                    maxHeight = cellHeight;
                }
            }
        }

        auto slope = maxHeight - minHeight;
        return slope;
    }

    bool isAreaUnderWater(
        const Grid<unsigned char>& heights,
        unsigned int waterLevel,
        unsigned int x,
        unsigned int y,
        unsigned int width,
        unsigned int height)
    {
        for (unsigned int dy = 0; dy < height + 1; ++dy)
        {
            for (unsigned int dx = 0; dx < width + 1; ++dx)
            {
                auto cellHeight = heights.get(x + dx, y + dy);
                if (cellHeight < waterLevel)
                {
                    return true;
                }
            }
        }

        return false;
    }
}
