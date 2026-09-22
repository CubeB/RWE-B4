#include "UnitPathFinder.h"
#include <algorithm>

namespace rwe
{
    UnitPathFinder::UnitPathFinder(
        const GameSimulation* simulation,
        const MovementClassCollisionService* collisionService,
        UnitId self,
        std::optional<MovementClassId> movementClass,
        unsigned int footprintX,
        unsigned int footprintZ,
        const Point& goal,
        AStarScratch* scratch)
        : AbstractUnitPathFinder(
            simulation,
            collisionService,
            self,
            movementClass,
            footprintX,
            footprintZ,
            scratch),
          goal(goal)
    {
    }

    void UnitPathFinder::setAcceptableDistance(unsigned int distance)
    {
        // The loosest of whatever is asked for, because two things now ask.
        // beginSearch relaxes a goal something is standing on to "next to
        // it", and the first pass then relaxes it again to whatever it could
        // actually walk to; taking the last would let the second TIGHTEN the
        // first and put the exhaustive search back.
        acceptableDistance = std::max(acceptableDistance, distance);
    }

    bool UnitPathFinder::isGoal(const Point& vertex)
    {
        if (vertex == goal)
        {
            return true;
        }

        if (acceptableDistance == 0)
        {
            return false;
        }

        return octileDistanceScore(vertex, goal) <= acceptableDistance;
    }

    PathCost UnitPathFinder::estimateCostToGoal(const Point& start)
    {
        auto distance = octileDistance(start, goal);
        unsigned int turns = (distance.straight > 0 && distance.diagonal > 0) ? 1 : 0;
        return PathCost(distance, turns);
    }
}
