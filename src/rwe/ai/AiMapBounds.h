#pragma once

#include <rwe/grid/DiscreteRect.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimVector.h>

namespace rwe
{
    /**
     * The part of the map a player can see and click on.
     *
     * The camera stops short of the last eight rows of height tiles along the
     * bottom and the last two columns along the right --
     * MapTerrain::bottomCutoffInWorldUnits and rightCutoffInWorldUnits, which
     * is what computeCameraConstraint clamps to -- so a player can neither
     * place a building there nor order a unit into it. The simulation allows
     * both: the original's placement test only keeps a footprint one cell off
     * the very edge (GameSimulation::isInsideBuildableArea). So this is the
     * computer player's rule and not the game's. Reported from a replay on
     * Great Divide, whose bottom start stands close enough to the cut-off
     * that the commander was walking out of sight to build, and fleeing into
     * the corner beyond it.
     */
    inline bool footprintInsideVisibleMap(const MapTerrain& terrain, const DiscreteRect& rect)
    {
        const auto& heights = terrain.getHeightMap();
        // A cell is visible if its far edge is no further than the cut-off,
        // which puts the last visible column at width - 3 and the last
        // visible row at height - 9, the heightmap being one point wider
        // than the cells on each axis.
        return rect.x >= 0 && rect.y >= 0
            && rect.x + rect.width <= heights.getWidth() - 2
            && rect.y + rect.height <= heights.getHeight() - 8;
    }

    /** A point pulled inside the visible map, `margin` in from its edges, standing on the ground. */
    inline SimVector clampInsideVisibleMap(const MapTerrain& terrain, const SimVector& p, SimScalar margin)
    {
        auto x = rweMin(rweMax(p.x, terrain.leftInWorldUnits() + margin), terrain.rightCutoffInWorldUnits() - margin);
        auto z = rweMin(rweMax(p.z, terrain.topInWorldUnits() + margin), terrain.bottomCutoffInWorldUnits() - margin);
        return SimVector(x, terrain.getHeightAt(x, z), z);
    }
}
