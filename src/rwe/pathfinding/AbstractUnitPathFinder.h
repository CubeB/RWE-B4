#pragma once

#include <rwe/grid/DiscreteRect.h>
#include <rwe/grid/EightWayDirection.h>
#include <rwe/pathfinding/AStarPathFinder.h>
#include <rwe/pathfinding/AStarScratch.h>
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
        const UnitId self;
        /**
         * The movement class's walkability grid, looked up once. Fetching it
         * through MovementClassCollisionService meant hashing the movement
         * class id on every one of the sixteen walkability questions a node
         * expansion used to ask.
         */
        const Grid<char>* const walkableGrid;
        const Grid<unsigned char>* const heights;
        const unsigned int seaLevel;
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
            unsigned int footprintZ,
            AStarScratch* scratch = nullptr);

        bool isWalkable(const Point& p) const;

    protected:
        unsigned int getSuccessors(const Point& vertex, const std::optional<Point>& predecessor, const PathCost& costToReach, Successor* out) override;

    private:
        /**
         * The three terrain questions the search asks of a cell, worked out on
         * first use and remembered on the cell for the rest of the search.
         *
         * Every cell is looked at by up to eight neighbours, and the corner
         * rule looks at the two cells flanking each diagonal on top of that,
         * so without this each answer was being recomputed around ten times
         * per search. The simulation does not change while a search runs, so a
         * remembered answer is the same answer.
         *
         * They take the cell rather than finding it, because one node
         * expansion asks all three of the same cell and the walk out to it is
         * the expensive part.
         */
        bool isWalkableAt(const Point& p, AStarScratch::Cell& cell) const;
        bool isRoughTerrainAt(const Point& p, AStarScratch::Cell& cell) const;
        bool isUnderWaterAt(const Point& p, AStarScratch::Cell& cell) const;

        bool computeWalkable(const Point& p) const;

        bool computeRoughTerrain(const Point& p) const;

        bool computeUnderWater(const Point& p) const;
    };
}
