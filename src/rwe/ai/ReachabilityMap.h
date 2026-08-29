#pragma once

#include <rwe/grid/Grid.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitDefinition.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * Which heightmap tiles a ground unit of the given movement class can
     * walk to from the base, ignoring units and buildings. Anything the AI
     * cannot walk to is an island (or the far side of a river) and needs a
     * transport.
     */
    class ReachabilityMap
    {
    public:
        void rebuild(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from);

        bool isValid() const { return reachable.getWidth() > 0; }

        /** True when a ground unit at the base can walk to this point. */
        bool isReachable(const GameSimulation& sim, const SimVector& position) const;

        /** True when a ground unit of the mover's class can stand here at all. */
        bool isWalkable(const GameSimulation& sim, const SimVector& position) const;

        int reachableTileCount() const { return reachableTiles; }
        int walkableTileCount() const { return walkableTiles; }

    private:
        Grid<unsigned char> walkable;
        Grid<unsigned char> reachable;
        int reachableTiles{0};
        int walkableTiles{0};
    };
}
