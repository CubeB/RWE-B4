#include "UnitPathFinder.h"

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
        acceptableDistance = distance;
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
