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

        bool computeWaterIsSlow(const GameSimulation& simulation, UnitId self)
        {
            // Floating units (ships) and units that need water under them
            // are at home in the water; everything else wades slowly.
            const auto& unit = simulation.getUnitState(self);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            if (unitDefinition.floater)
            {
                return false;
            }
            auto movementClass = simulation.getAdHocMovementClass(unitDefinition.movementCollisionInfo);
            return movementClass.minWaterDepth == 0;
        }

        const Grid<char>* findWalkableGrid(const MovementClassCollisionService* collisionService, const std::optional<MovementClassId>& movementClass)
        {
            return movementClass ? &collisionService->getGrid(*movementClass) : nullptr;
        }

        /**
         * The eight-way direction helpers, tabulated.
         *
         * They live in another translation unit as switches over the
         * direction, and a node expansion was making about fifteen of those
         * calls. The tables are filled by asking the real functions once, so
         * they cannot drift away from them.
         */
        struct DirectionTables
        {
            Point delta[8];
            bool diagonal[8];
            /** The step's own length: one straight, or one diagonal. */
            OctileDistance step[8];
            /** For a diagonal, the two cardinal directions it is made of. */
            unsigned int horizontalPart[8];
            unsigned int verticalPart[8];
            unsigned int turn[8][8];

            DirectionTables()
            {
                for (unsigned int i = 0; i < 8; ++i)
                {
                    auto d = static_cast<Direction>(i);
                    delta[i] = directionToPoint(d);
                    diagonal[i] = isDiagonal(d);
                    step[i] = octileDistance(Point(0, 0), delta[i]);
                    horizontalPart[i] = diagonal[i] ? directionToIndex(pointToDirection(Point(delta[i].x, 0))) : i;
                    verticalPart[i] = diagonal[i] ? directionToIndex(pointToDirection(Point(0, delta[i].y))) : i;
                    for (unsigned int j = 0; j < 8; ++j)
                    {
                        turn[i][j] = directionDistance(d, static_cast<Direction>(j));
                    }
                }
            }
        };

        const DirectionTables& directions()
        {
            static const DirectionTables tables;
            return tables;
        }
    }

    AbstractUnitPathFinder::AbstractUnitPathFinder(
        const GameSimulation* simulation,
        const MovementClassCollisionService* collisionService,
        UnitId self,
        std::optional<MovementClassId> movementClass,
        unsigned int footprintX,
        unsigned int footprintZ,
        AStarScratch* scratch)
        : AStarPathFinder<Point, PathCost>(scratch),
          simulation(simulation),
          self(self),
          walkableGrid(findWalkableGrid(collisionService, movementClass)),
          heights(&simulation->terrain.getHeightMap()),
          seaLevel(simScalarToUInt(simulation->terrain.getSeaLevel())),
          footprintX(footprintX),
          footprintZ(footprintZ),
          roughSlope(computeRoughSlope(*simulation, self)),
          waterIsSlow(computeWaterIsSlow(*simulation, self))
    {
        // The scratch is indexed by occupied-grid position, which is the only
        // region a walkable cell can lie in: a footprint that does not fit on
        // the occupied grid collides by definition.
        getScratch().configure(simulation->occupiedGrid.getWidth(), simulation->occupiedGrid.getHeight());
    }

    unsigned int AbstractUnitPathFinder::getSuccessors(const Point& vertex, const std::optional<Point>& predecessor, const PathCost& costToReach, Successor* out)
    {
        const auto& tables = directions();
        auto& scratch = getScratch();

        std::optional<unsigned int> prevDirection;
        if (predecessor)
        {
            prevDirection = directionToIndex(pointToDirection(vertex - *predecessor));
        }

        // Every neighbour is visited once, up front, and its cell held on to.
        // The corner rule below wants the walkability of the two cardinal
        // neighbours flanking each diagonal, and those are neighbours in their
        // own right, so asking here answers both questions from one visit; the
        // rough and underwater questions that follow then reuse the same cell
        // rather than walking out to it again. That is the difference between
        // fifteen cell visits per node expansion and eight.
        Point neighbours[8];
        AStarScratch::Cell* cells[8];
        bool walkable[8];
        for (unsigned int i = 0; i < 8; ++i)
        {
            neighbours[i] = vertex + tables.delta[i];
            auto index = scratch.toIndex(neighbours[i].x, neighbours[i].y);
            if (index < 0)
            {
                // Off the occupied grid, so the footprint cannot fit and the
                // cell can never be walkable. computeWalkable agrees, via
                // tryToRegion, but there is nowhere to remember the answer.
                cells[i] = nullptr;
                walkable[i] = false;
                continue;
            }

            cells[i] = &scratch.at(index);
            walkable[i] = isWalkableAt(neighbours[i], *cells[i]);
        }

        unsigned int count = 0;
        for (unsigned int i = 0; i < 8; ++i)
        {
            if (!walkable[i])
            {
                continue;
            }

            // No squeezing diagonally between two obstacles whose corners
            // touch: both of the orthogonal steps that make up the diagonal
            // must be clear too.
            if (tables.diagonal[i] && (!walkable[tables.horizontalPart[i]] || !walkable[tables.verticalPart[i]]))
            {
                continue;
            }

            const auto& neighbour = neighbours[i];
            auto distance = tables.step[i];
            auto stepCost = distance;
            if (isRoughTerrainAt(neighbour, *cells[i]))
            {
                // double the cost on rough terrain
                stepCost = stepCost + distance;
            }
            if (waterIsSlow && isUnderWaterAt(neighbour, *cells[i]))
            {
                // wading is slow too: a step through water costs twice a step on land
                stepCost = stepCost + distance;
            }

            unsigned int turns = prevDirection ? tables.turn[*prevDirection][i] : 0;
            out[count] = Successor{costToReach + PathCost(stepCost, turns), neighbour};
            count += 1;
        }

        return count;
    }

    bool AbstractUnitPathFinder::isWalkable(const Point& p) const
    {
        auto& scratch = getScratch();
        auto index = scratch.toIndex(p.x, p.y);
        if (index < 0)
        {
            return false;
        }

        return isWalkableAt(p, scratch.at(index));
    }

    bool AbstractUnitPathFinder::isWalkableAt(const Point& p, AStarScratch::Cell& cell) const
    {
        if (!(cell.flags & AStarScratch::WalkableKnown))
        {
            cell.flags |= AStarScratch::WalkableKnown;
            if (computeWalkable(p))
            {
                cell.flags |= AStarScratch::WalkableValue;
            }
        }

        return (cell.flags & AStarScratch::WalkableValue) != 0;
    }

    bool AbstractUnitPathFinder::isRoughTerrainAt(const Point& p, AStarScratch::Cell& cell) const
    {
        if (!(cell.flags & AStarScratch::RoughKnown))
        {
            cell.flags |= AStarScratch::RoughKnown;
            if (computeRoughTerrain(p))
            {
                cell.flags |= AStarScratch::RoughValue;
            }
        }

        return (cell.flags & AStarScratch::RoughValue) != 0;
    }

    bool AbstractUnitPathFinder::isUnderWaterAt(const Point& p, AStarScratch::Cell& cell) const
    {
        if (!(cell.flags & AStarScratch::WaterKnown))
        {
            cell.flags |= AStarScratch::WaterKnown;
            if (computeUnderWater(p))
            {
                cell.flags |= AStarScratch::WaterValue;
            }
        }

        return (cell.flags & AStarScratch::WaterValue) != 0;
    }

    bool AbstractUnitPathFinder::isWalkableOutsideSearch(const Point& p) const
    {
        return computeWalkable(p);
    }

    bool AbstractUnitPathFinder::computeWalkable(const Point& p) const
    {
        DiscreteRect rect(p.x, p.y, footprintX, footprintZ);
        return (walkableGrid == nullptr || walkableGrid->tryGetValue(p).value_or(false))
            && !simulation->isCollisionAt(rect, self);
    }

    bool AbstractUnitPathFinder::computeRoughTerrain(const Point& p) const
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
        auto x = static_cast<unsigned int>(p.x);
        auto y = static_cast<unsigned int>(p.y);
        if (x + footprintX >= heights->getWidth() || y + footprintZ >= heights->getHeight())
        {
            return false;
        }

        // This is isMaxSlopeGreaterThan with the same limit above and below the
        // waterline, which is how it has always been called from here. With the
        // two limits equal its underwater test cannot change the answer, and
        // running it was scanning the footprint a second time for nothing.
        for (unsigned int dy = 0; dy < footprintZ; ++dy)
        {
            for (unsigned int dx = 0; dx < footprintX; ++dx)
            {
                if (getSlope(*heights, x + dx, y + dy) > roughSlope)
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool AbstractUnitPathFinder::computeUnderWater(const Point& p) const
    {
        if (p.x < 0 || p.y < 0)
        {
            return false;
        }

        auto x = static_cast<unsigned int>(p.x);
        auto y = static_cast<unsigned int>(p.y);
        // isAreaUnderWater looks one cell past the footprint on each axis.
        if (x + footprintX >= static_cast<unsigned int>(heights->getWidth()) || y + footprintZ >= static_cast<unsigned int>(heights->getHeight()))
        {
            return false;
        }

        return isAreaUnderWater(*heights, seaLevel, x, y, footprintX, footprintZ);
    }
}
