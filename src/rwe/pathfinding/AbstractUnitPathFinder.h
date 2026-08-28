#pragma once

#include <rwe/grid/DiscreteRect.h>
#include <rwe/grid/EightWayDirection.h>
#include <rwe/pathfinding/AStarPathFinder.h>
#include <rwe/pathfinding/PathCost.h>
#include <rwe/pathfinding/pathfinding_utils.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    /**
     * Standard unit pathfinder.
     */
    class AbstractUnitPathFinder : public AStarPathFinder<Point, PathCost>
    {
    private:
        const GameSimulation* const simulation;
        const MovementClassCollisionService* const collisionService;
        const UnitId self;
        const std::optional<MovementClassId> movementClass;
        const unsigned int footprintX;
        const unsigned int footprintZ;
        /** Slopes steeper than this count as rough terrain (double cost). */
        const unsigned int roughSlope;
        /**
         * True for land units that can wade: water costs them double,
         * so a dry route is preferred when one exists.
         * Ships and other units that live in the water are never penalised.
         */
        const bool waterIsSlow;

    public:
        AbstractUnitPathFinder(
            const GameSimulation* simulation,
            const MovementClassCollisionService* collisionService,
            UnitId self,
            std::optional<MovementClassId> movementClass,
            unsigned int footprintX,
            unsigned int footprintZ);

        bool isWalkable(const Point& p) const;

    protected:
        std::vector<VertexInfo> getSuccessors(const VertexInfo& vertex) override;

    private:
        bool isWalkable(int x, int y) const;

        bool isRoughTerrain(const Point& p) const;

        bool isUnderWater(const Point& p) const;

        Point step(const Point& p, Direction d) const;

        std::vector<Point> getNeighbours(const Point& p);
    };
}
