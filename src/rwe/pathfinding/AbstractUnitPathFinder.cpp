#include "AbstractUnitPathFinder.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/movement.h>

namespace rwe
{
    namespace
    {
        MovementClassDefinition movementClassOf(const GameSimulation& simulation, UnitId self)
        {
            const auto& unit = simulation.getUnitState(self);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            return simulation.getAdHocMovementClass(unitDefinition.movementCollisionInfo);
        }

        /** The lowest of a cell's four corners, which is what the original's cell test reads at square+0x06. */
        unsigned int lowCorner(const Grid<unsigned char>& heights, unsigned int x, unsigned int y)
        {
            return std::min({heights.get(x, y), heights.get(x + 1, y), heights.get(x, y + 1), heights.get(x + 1, y + 1)});
        }

        /** The highest of a cell's four corners, read at square+0x05 for the minimum-depth test. */
        unsigned int highCorner(const Grid<unsigned char>& heights, unsigned int x, unsigned int y)
        {
            return std::max({heights.get(x, y), heights.get(x + 1, y), heights.get(x, y + 1), heights.get(x + 1, y + 1)});
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
        std::optional<MovementClassId> movementClassId,
        unsigned int footprintX,
        unsigned int footprintZ,
        AStarScratch* scratch)
        : AStarPathFinder<Point, PathCost>(scratch),
          simulation(simulation),
          self(self),
          walkableGrid(findWalkableGrid(collisionService, movementClassId)),
          heights(&simulation->terrain.getHeightMap()),
          seaLevel(simScalarToUInt(simulation->terrain.getSeaLevel())),
          footprintX(footprintX),
          footprintZ(footprintZ),
          selfRegion(simulation->computeFootprintRegion(simulation->getUnitState(self).position, footprintX, footprintZ)),
          movementClass(movementClassOf(*simulation, self)),
          waterIsSlow(computeWaterIsSlow(*simulation, self)),
          searchTimeValue(simulation->gameTime)
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

    bool AbstractUnitPathFinder::isRoughTerrainOutsideSearch(const Point& p) const
    {
        return computeRoughTerrain(p);
    }

    bool AbstractUnitPathFinder::computeWalkable(const Point& p) const
    {
        DiscreteRect rect(p.x, p.y, footprintX, footprintZ);
        return (walkableGrid == nullptr || walkableGrid->tryGetValue(p).value_or(false))
            && !simulation->isCollisionAtForSearch(rect, self, selfRegion, searchTimeValue);
    }

    bool AbstractUnitPathFinder::computeRoughTerrain(const Point& p) const
    {
        DiscreteRect rect(p.x, p.y, footprintX, footprintZ);

        // BT-52: the original's tight class is decided by the ring around the
        // footprint, not just by the footprint's own slope. Each of the four
        // one-cell strips is run through the same rectangle test as the
        // footprint, and any strip that is not completely free (blocked, off
        // the map, too deep or shallow, or over BadSlope) downgrades the cell
        // to tight. The pinwheel is exact: each corner belongs to one strip,
        // and the bottom strip is w+1 wide at y+h, not y+w.
        const DiscreteRect top(rect.x - 1, rect.y - 1, static_cast<int>(footprintX) + 1, 1);
        const DiscreteRect right(rect.x + static_cast<int>(footprintX), rect.y - 1, 1, static_cast<int>(footprintZ) + 1);
        const DiscreteRect bottom(rect.x, rect.y + static_cast<int>(footprintZ), static_cast<int>(footprintX) + 1, 1);
        const DiscreteRect left(rect.x - 1, rect.y, 1, static_cast<int>(footprintZ) + 1);
        if (!isClearanceStripFree(top) || !isClearanceStripFree(right) || !isClearanceStripFree(bottom) || !isClearanceStripFree(left))
        {
            return true;
        }

        if (p.x < 0 || p.y < 0)
        {
            return false;
        }

        // Steep (but passable) ground is slow to cross, so prefer routes around it.
        auto x = static_cast<unsigned int>(p.x);
        auto y = static_cast<unsigned int>(p.y);
        if (x + footprintX >= static_cast<unsigned int>(heights->getWidth()) || y + footprintZ >= static_cast<unsigned int>(heights->getHeight()))
        {
            return false;
        }

        // The original's cell test (0x47E145-0x47E19E) picks the dry or the
        // wet pair per cell: a cell whose lowest corner is at or above sea
        // level is dry and measured against BadSlope, anything else is wet
        // and measured against BadWaterSlope. It used to be one threshold,
        // half of MaxSlope, above and below the waterline.
        for (unsigned int dy = 0; dy < footprintZ; ++dy)
        {
            for (unsigned int dx = 0; dx < footprintX; ++dx)
            {
                auto threshold = lowCorner(*heights, x + dx, y + dy) >= seaLevel ? movementClass.badSlope : movementClass.badWaterSlope;
                if (getSlope(*heights, x + dx, y + dy) > threshold)
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool AbstractUnitPathFinder::isClearanceStripFree(const DiscreteRect& rect) const
    {
        // The rectangle routine refuses a rectangle that is off the map, and
        // a refused strip is never free. Checking first also keeps the cell
        // reads below inside the heightmap's 2x2 blocks.
        if (rect.x < 0 || rect.y < 0
            || rect.x + rect.width >= static_cast<int>(heights->getWidth())
            || rect.y + rect.height >= static_cast<int>(heights->getHeight()))
        {
            return false;
        }

        // The same occupant, building, blocking-feature and lava test as the
        // footprint. The search's stamp rule applies here too (BT-58/BT-59):
        // a mobile unit that moved within the last thirty ticks is not a
        // wall, a stationary one is.
        if (simulation->isCollisionAtForSearch(rect, self, selfRegion, searchTimeValue))
        {
            return false;
        }

        for (int y = rect.y; y < rect.y + rect.height; ++y)
        {
            for (int x = rect.x; x < rect.x + rect.width; ++x)
            {
                if (!isClearanceCellFree(static_cast<unsigned int>(x), static_cast<unsigned int>(y)))
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool AbstractUnitPathFinder::isClearanceCellFree(unsigned int x, unsigned int y) const
    {
        const auto low = lowCorner(*heights, x, y);
        const auto high = highCorner(*heights, x, y);

        // The original tests the worst corner for each depth bound:
        // square+6 >= seaLevel - MaxWaterDepth and
        // square+5 <= seaLevel - MinWaterDepth. MinWaterDepth is signed and
        // absent reads as -10000, which is what lets a land unit cross dry
        // ground even though a boat's written 0 does not.
        const auto sea = static_cast<int>(seaLevel);
        if (static_cast<int>(low) < sea - static_cast<int>(movementClass.maxWaterDepth))
        {
            return false;
        }
        if (static_cast<int>(high) > sea - movementClass.signedMinWaterDepth())
        {
            return false;
        }

        auto threshold = low >= seaLevel ? movementClass.badSlope : movementClass.badWaterSlope;
        return getSlope(*heights, x, y) <= threshold;
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
