#pragma once

#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/SimVector.h>

namespace rwe
{
    /**
     * What one player can see of the map.
     *
     * Kept at a coarser resolution than the heightmap (VisionCellSizeInTiles
     * heightmap tiles per cell) so that stamping every unit's sight circle
     * each tick stays cheap even on large maps.
     */
    struct PlayerVisibility
    {
        static constexpr int VisionCellSizeInTiles = 2;

        /** Cells that have been seen at some point. Never cleared. */
        Grid<unsigned char> explored;

        /** Cells currently within line of sight of a unit. Rebuilt every tick. */
        Grid<unsigned char> visible;

        /** Cells currently covered by radar. Rebuilt every tick. */
        Grid<unsigned char> radar;

        PlayerVisibility() = default;
        PlayerVisibility(int width, int height);

        bool contains(const Point& cell) const;
        bool isExplored(const Point& cell) const;
        bool isVisible(const Point& cell) const;
        bool isOnRadar(const Point& cell) const;

        void clearCurrent();

        /** Marks every cell within radius (in cells) of the centre as visible and explored. */
        void revealCircle(const Point& center, int radius);

        /**
         * Like revealCircle, but a cell is only revealed if the straight line
         * from an eye eyeHeight above the centre cell's ground to a point
         * targetHeight above the cell's ground clears the ground in between.
         * Hills and ridges therefore cast shadows.
         */
        void revealCircleWithLineOfSight(const Point& center, int radius, const Grid<unsigned char>& groundHeights, int eyeHeight, int targetHeight);

        /** Marks every cell within radius (in cells) of the centre as radar-covered. */
        void radarCircle(const Point& center, int radius);
    };

    /** Ground height per vision cell: the highest heightmap sample the cell covers. */
    Grid<unsigned char> computeVisionHeights(const Grid<unsigned char>& heightmap);
}
