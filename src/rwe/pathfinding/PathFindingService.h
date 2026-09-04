#pragma once

#include <rwe/grid/DiscreteRect.h>
#include <rwe/grid/Point.h>
#include <rwe/pathfinding/AStarPathFinder.h>
#include <rwe/pathfinding/AStarScratch.h>
#include <rwe/pathfinding/PathCost.h>
#include <rwe/pathfinding/UnitPath.h>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;

    class PathFindingService
    {
    public:
        AStarPathInfo<Point, PathCost> lastPathDebugInfo;

        void update(GameSimulation& simulation);

    private:
        /**
         * Working storage shared by every search, so that a search allocates
         * nothing. It is scratch in the strictest sense -- each search stamps
         * what it touches with its own number and reads nothing left behind by
         * the last one -- so sharing it cannot make one path depend on another.
         */
        AStarScratch scratch;

        UnitPath findPath(const GameSimulation& simulation, UnitId unitId, const SimVector& destination);
        UnitPath findPath(const GameSimulation& simulation, UnitId unitId, const DiscreteRect& destination);

        SimVector getWorldCenter(const GameSimulation& simulation, const DiscreteRect& discreteRect);
    };
}
