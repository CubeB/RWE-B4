#pragma once

#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/MovementClassId.h>
#include <unordered_map>

namespace rwe
{
    class MovementClassCollisionService
    {
    private:
        std::unordered_map<MovementClassId, Grid<char>> walkableGrids;

        /**
         * The connected regions of each movement class's terrain, computed
         * from its walkable grid on first use and kept.
         *
         * Cell 0 is not walkable; every other cell holds the id of the region
         * it belongs to. This is what lets a search prove a goal unreachable
         * without running: two cells in different regions cannot be joined by
         * any route, whatever the units are doing.
         *
         * The connectivity is the search's own -- eight-way, with a diagonal
         * edge only when both of the orthogonal cells between its ends are
         * walkable too -- because a four-way labelling would call two cells
         * separate that a diagonal step in fact joins, and a precheck that
         * says "unreachable" wrongly is worse than no precheck at all.
         *
         * Derived from the walkable grid, which is derived from the terrain,
         * so it is a pure function of saved state: every peer labels it the
         * same and it is neither saved nor hashed. When it is first computed
         * is a performance detail and cannot change an outcome.
         */
        mutable std::unordered_map<MovementClassId, Grid<int>> componentGrids;

    public:
        void registerMovementClass(MovementClassId id, Grid<char>&& walkableGrid);

        bool isWalkable(MovementClassId movementClass, const Point& position) const;

        const Grid<char>& getGrid(MovementClassId movementClass) const;

        /**
         * The region grid for a movement class, labelled on first use, or
         * null when the class has no registered walkable grid.
         */
        const Grid<int>* tryGetComponentGrid(MovementClassId movementClass) const;
    };

    Grid<char> computeWalkableGrid(const MapTerrain& terrain, const MovementClassDefinition& movementClass);
}
