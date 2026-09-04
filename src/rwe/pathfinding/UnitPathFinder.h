#pragma once

#include <rwe/grid/Point.h>
#include <rwe/pathfinding/AbstractUnitPathFinder.h>
#include <rwe/pathfinding/PathCost.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    /**
     * Standard unit pathfinder.
     */
    class UnitPathFinder : public AbstractUnitPathFinder
    {
    private:
        const Point goal;

        /**
         * How close counts as arrival. Zero is the exact goal and is the
         * usual case; a larger number is the original's relaxed goal, set
         * from how close its cheap first pass could get when the goal itself
         * cannot be reached (0x40DCA8).
         */
        unsigned int acceptableDistance{0};

    public:
        /** Accept any cell within this octile distance of the goal. */
        void setAcceptableDistance(unsigned int distance);

        UnitPathFinder(
            const GameSimulation* simulation,
            const MovementClassCollisionService* collisionService,
            UnitId self,
            std::optional<MovementClassId> movementClass,
            unsigned int footprintX,
            unsigned int footprintZ,
            const Point& goal,
            AStarScratch* scratch = nullptr);

    protected:
        bool isGoal(const Point& vertex) override;

        PathCost estimateCostToGoal(const Point& start) override;
    };
}
