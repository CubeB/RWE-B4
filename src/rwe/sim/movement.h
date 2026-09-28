#pragma once

#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/MovementClassId.h>
#include <unordered_map>

namespace rwe
{
    /**
     * Whether a unit of this movement class can stand with its footprint's
     * top-left tile at (x,y): the slope of every cell of the footprint within
     * the limit that applies to it, and every cell's depth within the
     * movement class's own bounds.
     *
     * This is the question the AI's build-site searches, the reachability
     * labelling and the movement-class collision grids all ask, over large
     * candidate sets, and both halves answer from MapTerrain's cached
     * per-cell slope and water-depth grids. Those grids are getSlope's and
     * getWaterDepth's arithmetic done once per cell at load, so the two
     * functions below are the geometric rule unchanged and the cache is only
     * where the per-cell numbers come from. movement.test.cpp walks a map
     * holding the cached values against the geometric ones.
     */
    bool isGridPointWalkable(const MapTerrain& terrain, const MovementClassDefinition& movementClass, unsigned int x, unsigned int y);

    /** Whether any cell of the footprint at (x,y) is steeper than the limit that applies to it. */
    bool isMaxSlopeGreaterThan(const MapTerrain& terrain, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int maxSlope, unsigned int maxWaterSlope);

    /** Whether every cell of the footprint at (x,y) is between minWaterDepth and maxWaterDepth deep. */
    bool isWaterDepthWithinBounds(const MapTerrain& terrain, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int minWaterDepth, unsigned int maxWaterDepth);

    /**
     * The water depth of one cell: the sea level minus the cell's height,
     * floored at zero. What MapTerrain's cached depth grid holds, and what
     * MapTerrain::getWaterDepthAt falls back to when the sea level is above
     * 255 and a depth would not fit a byte.
     */
    unsigned int getWaterDepth(const Grid<unsigned char>& heights, unsigned int waterLevel, unsigned int x, unsigned int y);

    /**
     * The slope of one cell: the height range over the 2x2 block of corners
     * at (x,y)..(x+1,y+1). What MapTerrain's cached slope grid holds.
     *
     * The pathfinder reads this directly rather than through
     * isMaxSlopeGreaterThan: it asks about a cell it is standing beside
     * rather than about a whole footprint, memoised per cell for the length
     * of a search, so there is no repeated walk for a cache to amortise.
     */
    unsigned int getSlope(const Grid<unsigned char>& heights, unsigned int x, unsigned int y);

    /** Whether any cell of the (width+1)x(height+1) block whose top-left corner is (x,y) is below the water level. */
    bool isAreaUnderWater(const Grid<unsigned char>& heights, unsigned int waterLevel, unsigned int x, unsigned int y, unsigned int width, unsigned int height);
}
