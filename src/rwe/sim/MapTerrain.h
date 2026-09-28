#pragma once

#include <rwe/geometry/Line3f.h>
#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/render/TextureRegion.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <vector>

namespace rwe
{
    class MapTerrain
    {
    public:
        static constexpr SimScalar HeightTileWidthInWorldUnits = 16_ss;
        static constexpr SimScalar HeightTileHeightInWorldUnits = 16_ss;

        static constexpr SimScalar MaxHeight = 255_ss;
        static constexpr SimScalar MinHeight = 0_ss;

    private:
        Grid<unsigned char> heights;

        /**
         * Slope per cell, cached: the height range over the 2x2 block of
         * corners at (x,y)..(x+1,y+1). One cell narrower than the
         * heightmap on each axis, because that block at the last row or
         * column would read past the map.
         *
         * A pure function of `heights`, which nothing can change after
         * construction -- getHeightMap hands out a const reference and
         * there is no other way in -- so this is built once and never
         * rebuilt. It replaces the 2x2 walk getSlope did per cell, which
         * is the bulk of what isGridPointWalkable and canBeBuiltAt cost.
         */
        Grid<unsigned char> slopeCache;

        /**
         * Water depth per cell, cached: the sea level minus the cell's
         * height, floored at zero. A pure function of `heights` and the
         * sea level, and so immutable for the same reason as slopeCache.
         *
         * EMPTY, rather than wrong, when the sea level is above 255. The
         * sea level comes off a map header as a 32-bit field, so a
         * malformed map can ask for a depth that does not fit a byte, and
         * the movement classes compare a depth against MinWaterDepth and
         * MaxWaterDepth rather than merely asking whether a cell is wet --
         * so a wrapped 300 read back as 44 would be a different answer
         * rather than a slower one. getWaterDepthAt notices the empty grid
         * and does the subtraction itself, which is what every caller did
         * before there was a cache to notice.
         */
        Grid<unsigned char> waterDepthCache;

        SimScalar seaLevel;

    public:
        MapTerrain(
            Grid<unsigned char>&& heights,
            SimScalar seaLevel);

        Point worldToHeightmapCoordinate(const SimVector& position) const;

        Point worldToHeightmapCoordinateNearest(const SimVector& position) const;

        SimVector heightmapIndexToWorldCorner(int x, int y) const;

        SimVector heightmapIndexToWorldCorner(Point p) const;

        SimVector heightmapIndexToWorldCenter(int x, int y) const;

        SimVector heightmapIndexToWorldCenter(std::size_t x, std::size_t y) const;

        SimVector heightmapIndexToWorldCenter(Point p) const;

        SimVector worldToHeightmapSpace(const SimVector& v) const;

        SimVector heightmapToWorldSpace(const SimVector& v) const;

        SimVector topLeftCoordinateToWorld(const SimVector& position) const;


        const Grid<unsigned char>& getHeightMap() const;

        /**
         * The cached per-cell slope grid, one cell narrower than the
         * heightmap on each axis. Empty on a map too small to hold a 2x2
         * block, which is the same set of cells on which getSlope on the
         * heightmap would have tripped Grid's own bounds assertion -- so a
         * caller reading past the end of this grid is caught where it always
         * was, rather than being answered out of range.
         */
        const Grid<unsigned char>& getSlopeMap() const;

        /**
         * The cached per-cell water depth grid, or a reference to an EMPTY
         * grid when the sea level is above 255 and a depth would not fit a
         * byte. Callers must check getWidth() before reading it, and fall
         * back to the heightmap when it is empty.
         */
        const Grid<unsigned char>& getWaterDepthMap() const;

        /** The water depth of one cell, from the cache where there is one. */
        unsigned int getWaterDepthAt(int x, int y) const;

        SimScalar leftInWorldUnits() const;
        SimScalar rightCutoffInWorldUnits() const;
        SimScalar topInWorldUnits() const;
        SimScalar bottomCutoffInWorldUnits() const;

        SimScalar getWidthInWorldUnits() const;
        SimScalar getHeightInWorldUnits() const;

        SimScalar getHalfWidthInWorldUnits() const;
        SimScalar getHalfHeightInWorldUnits() const;

        /**
         * Gets the height of the terrain at the given world coordinates.
         * If the input is outside the heightmap grid, returns 0.
         */
        SimScalar getHeightAt(SimScalar x, SimScalar z) const;

        /**
         * Gets the height of the terrain at the given world coordinates.
         * If the input is outside the heightmap grid, returns None.
         */
        std::optional<SimScalar> tryGetHeightAt(SimScalar x, SimScalar z) const;

        std::optional<SimVector> intersectLine(const Line3x<SimScalar>& line) const;

        std::optional<SimVector> intersectWithHeightmapCell(const Line3x<SimScalar>& line, int x, int y) const;

        SimScalar getSeaLevel() const;

        /**
         * Whether the square under a point lies wholly under the sea: the
         * highest of its four corners is below sea level. This, and not which
         * surface a round struck, is what decides whether its detonation
         * throws up spray or earth (0x499ECF, reading the high corner the map
         * loader stores at 0x483329). Off the map is dry, and so is the last
         * row and column of heightmap corners, which start no square.
         */
        bool isSquareUnderSea(SimScalar x, SimScalar z) const;

    private:
        bool isInHeightMapBounds(int x, int y) const;
    };
}
