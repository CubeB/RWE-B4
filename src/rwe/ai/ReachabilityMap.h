#pragma once

#include <optional>
#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitDefinition.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * Which heightmap tiles a ground unit of the given movement class can
     * walk to from the base, ignoring units and buildings. Anything the AI
     * cannot walk to is an island (or the far side of a river) and needs a
     * transport.
     *
     * The terrain never changes during a game, so the expensive part -
     * labelling every walkable tile with the connected region it belongs to -
     * is done once per movement class and then reused. Re-homing the map on a
     * different base anchor afterwards is a handful of lookups.
     */
    class ReachabilityMap
    {
    public:
        /**
         * Labels the terrain the first time it is called for a movement class,
         * then homes the map on the given position. Cheap on every call after
         * the first unless the movement class changes.
         */
        void rebuild(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from);

        bool isValid() const { return components.getWidth() > 0; }

        /** True when a ground unit at the base can walk to this point. */
        bool isReachable(const GameSimulation& sim, const SimVector& position) const;

        /** True when a ground unit of the mover's class can stand here at all. */
        bool isWalkable(const GameSimulation& sim, const SimVector& position) const;

        int reachableTileCount() const { return reachableTiles; }
        int walkableTileCount() const { return walkableTiles; }

    private:
        /** The one-off flood fill that labels every walkable tile with a region id. */
        void labelComponents(const GameSimulation& sim, const MovementClassDefinition& mc);

        /** Works out which regions count as home, given where the base is. */
        void setAnchor(const GameSimulation& sim, const SimVector& from);

        /** 0 where a ground unit cannot stand; otherwise the id of the connected region. */
        Grid<int> components;
        /** Tile count of each region, indexed by id - 1. */
        std::vector<int> componentSizes;
        /** The regions the base can walk in. Normally one; more only when the base straddles a gap. */
        std::vector<int> homeComponents;
        /** The base's own tile counts as home even where a unit could not stand on it. */
        std::optional<Point> anchorTile;

        /** The movement class the labelling was done for, so it is not redone needlessly. */
        std::optional<MovementClassDefinition> labelledFor;
        int walkableTiles{0};
        int reachableTiles{0};
    };
}
