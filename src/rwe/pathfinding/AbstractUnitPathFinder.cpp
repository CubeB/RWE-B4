#include "AbstractUnitPathFinder.h"
#include <rwe/sim/movement.h>

namespace rwe
{
    namespace
    {
        unsigned int computeRoughSlope(const GameSimulation& simulation, UnitId self)
        {
            // Half the unit's slope limit: climbable, but slow going.
            // Units that can go anywhere (max slope 255) never see rough terrain.
            const auto& unit = simulation.getUnitState(self);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            auto maxSlope = simulation.getAdHocMovementClass(unitDefinition.movementCollisionInfo).maxSlope;
            return maxSlope >= 255 ? 255 : maxSlope / 2;
        }
    }

    AbstractUnitPathFinder::AbstractUnitPathFinder(
        const GameSimulation* simulation,
        const MovementClassCollisionService* collisionService,
        UnitId self,
        std::optional<MovementClassId> movementClass,
        unsigned int footprintX,
        unsigned int footprintZ)
        : simulation(simulation),
          collisionService(collisionService),
          self(self),
          movementClass(movementClass),
          footprintX(footprintX),
          footprintZ(footprintZ),
          roughSlope(computeRoughSlope(*simulation, self))
    {
    }

    std::vector<AbstractUnitPathFinder::VertexInfo>
    AbstractUnitPathFinder::getSuccessors(const VertexInfo& info)
    {
        std::optional<Direction> prevDirection;
        if (info.predecessor)
        {
            prevDirection = pointToDirection(info.vertex - (*info.predecessor)->vertex);
        }

        auto neighbours = getNeighbours(info.vertex);

        std::vector<VertexInfo> vs;
        for (const auto& neighbour : neighbours)
        {
            auto direction = pointToDirection(neighbour - info.vertex);
            auto distance = octileDistance(info.vertex, neighbour);
            assert(distance.diagonal == 0 || distance.straight == 0);
            if (isRoughTerrain(neighbour))
            {
                // double the cost on rough terrain
                distance = distance + distance;
            }
            unsigned int turns = prevDirection ? directionDistance(*prevDirection, direction) : 0;
            PathCost cost(distance, turns);
            vs.push_back(VertexInfo{info.costToReach + cost, neighbour, &info});
        }

        return vs;
    }

    bool AbstractUnitPathFinder::isWalkable(const Point& p) const
    {
        DiscreteRect rect(p.x, p.y, footprintX, footprintZ);
        return (movementClass ? collisionService->isWalkable(*movementClass, p) : true) && !simulation->isCollisionAt(rect, self);
    }

    bool AbstractUnitPathFinder::isWalkable(int x, int y) const
    {
        return isWalkable(Point(x, y));
    }

    bool AbstractUnitPathFinder::isRoughTerrain(const Point& p) const
    {
        DiscreteRect rect(p.x, p.y, footprintX, footprintZ);
        if (simulation->isAdjacentToObstacle(rect))
        {
            return true;
        }

        if (roughSlope >= 255 || p.x < 0 || p.y < 0)
        {
            return false;
        }

        // Steep (but passable) ground is slow to cross, so prefer routes around it.
        const auto& heights = simulation->terrain.getHeightMap();
        auto x = static_cast<unsigned int>(p.x);
        auto y = static_cast<unsigned int>(p.y);
        if (x + footprintX >= heights.getWidth() || y + footprintZ >= heights.getHeight())
        {
            return false;
        }
        auto seaLevel = simScalarToUInt(simulation->terrain.getSeaLevel());
        return isMaxSlopeGreaterThan(heights, seaLevel, x, y, footprintX, footprintZ, roughSlope, roughSlope);
    }

    Point AbstractUnitPathFinder::step(const Point& p, Direction d) const
    {
        auto directionVector = directionToPoint(d);
        return p + directionVector;
    }

    std::vector<Point> AbstractUnitPathFinder::getNeighbours(const Point& p)
    {
        std::vector<Point> neighbours;
        neighbours.reserve(Directions.size());

        for (auto d : Directions)
        {
            auto newPosition = step(p, d);

            if (!isWalkable(newPosition))
            {
                continue;
            }

            // No squeezing diagonally between two obstacles whose corners
            // touch: both of the orthogonal steps that make up the diagonal
            // must be clear too.
            if (isDiagonal(d))
            {
                auto delta = directionToPoint(d);
                if (!isWalkable(p + Point(delta.x, 0)) || !isWalkable(p + Point(0, delta.y)))
                {
                    continue;
                }
            }

            neighbours.push_back(newPosition);
        }

        return neighbours;
    }
}
