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

        /** Marks every cell within radius (in cells) of the centre as radar-covered. */
        void radarCircle(const Point& center, int radius);
    };
}
