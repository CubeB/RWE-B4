#include "OaUnitPathSearch.h"

#include <rwe/pathfinding/AbstractUnitPathFinder.h>
#include <rwe/pathfinding/UnitPathFinder.h>
#include <rwe/pathfinding/oa/OaSearchGoal.h>
#include <rwe/pathfinding/oa/OaSearchWorker.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimScalar.h>

namespace rwe
{
    struct OaUnitPathSearch::Impl
    {
        const GameSimulation* simulation{};
        UnitId self{0};
        std::unique_ptr<UnitPathFinder> finder;
        oa::SearchWorker worker;
        oa::SearchGoal goal;
        oa::SearchUnit unit;
        oa::SearchBegin begin;
        bool finished{true};
        std::size_t expansions{0};

        uint32_t classify(int32_t x, int32_t z) const
        {
            Point p(x, z);
            if (!finder->isWalkableOutsideSearch(p))
            {
                return 0;
            }
            if (finder->isRoughTerrainOutsideSearch(p))
            {
                return 1;
            }
            return 3;
        }
    };

    OaUnitPathSearch::OaUnitPathSearch(
        const GameSimulation* simulation,
        UnitId self,
        std::optional<MovementClassId> movementClass,
        unsigned int footprintX,
        unsigned int footprintZ,
        const Point& goalCell,
        int32_t goalTolerance)
        : impl(std::make_unique<Impl>())
    {
        impl->simulation = simulation;
        impl->self = self;
        impl->finder = std::make_unique<UnitPathFinder>(
            simulation,
            &simulation->movementClassCollisionService,
            self,
            movementClass,
            footprintX,
            footprintZ,
            goalCell,
            nullptr);

        impl->goal.cellX = goalCell.x;
        impl->goal.cellZ = goalCell.y;
        impl->goal.tolerance = goalTolerance;
        const auto radiusCells = goalTolerance / 16;
        impl->goal.radiusSquared = radiusCells * radiusCells;

        impl->unit.footprintX = static_cast<int16_t>(footprintX);
        impl->unit.footprintZ = static_cast<int16_t>(footprintZ);

        impl->begin.mapWidth = static_cast<int32_t>(simulation->occupiedGrid.getWidth());
        impl->begin.mapHeight = static_cast<int32_t>(simulation->occupiedGrid.getHeight());
        impl->begin.classify = &OaUnitPathSearch::classifyThunk;
        impl->begin.classifyContext = impl.get();
    }

    OaUnitPathSearch::~OaUnitPathSearch() = default;
    OaUnitPathSearch::OaUnitPathSearch(OaUnitPathSearch&&) noexcept = default;
    OaUnitPathSearch& OaUnitPathSearch::operator=(OaUnitPathSearch&&) noexcept = default;

    void OaUnitPathSearch::beginSearch(const Point& start)
    {
        impl->unit.cellX = static_cast<int16_t>(start.x);
        impl->unit.cellZ = static_cast<int16_t>(start.y);
        impl->unit.heading = impl->simulation->getUnitState(impl->self).rotation.value;
        impl->expansions = 0;
        impl->finished = false;

        const oa::SearchRecord record{&impl->unit, &impl->goal};
        const auto advance = impl->worker.begin(record, impl->begin);
        impl->finished = advance != oa::SearchAdvance::in_progress;
    }

    unsigned int OaUnitPathSearch::stepSearch(unsigned int maxExpansions)
    {
        if (impl->finished)
        {
            return 0;
        }

        unsigned int done = 0;
        while (done < maxExpansions)
        {
            if (impl->worker.exhausted())
            {
                // Nowhere left to look: the goal cannot be reached. begin()
                // cleared the route, so an exhausted job has none.
                impl->finished = true;
                break;
            }

            ++done;
            if (impl->worker.expand_best() != 0)
            {
                impl->worker.publish_route();
                impl->finished = true;
                break;
            }
            impl->worker.set_fan(oa::search_continue_fan);
        }

        impl->expansions += done;
        return done;
    }

    bool OaUnitPathSearch::isSearchFinished() const
    {
        return impl->finished;
    }

    std::size_t OaUnitPathSearch::expansionsSoFar() const
    {
        return impl->expansions;
    }

    bool OaUnitPathSearch::goalWasRelaxed() const
    {
        return impl->worker.threshold() > 0;
    }

    UnitPath OaUnitPathSearch::takeResult(const GameSimulation& simulation) const
    {
        UnitPath path;
        const auto& route = impl->worker.route();
        // The route ends on the exact goal cell only when the search really
        // reached it; a relaxed goal finishes next to it, and the caller must
        // not snap the last waypoint onto a cell the unit cannot stand on.
        const auto finish = impl->worker.finish_cell();
        const bool finishedOnGoal = !route.empty()
            && finish[0] == impl->goal.cellX && finish[1] == impl->goal.cellZ;
        path.destinationUnreachable = !finishedOnGoal;
        if (route.empty())
        {
            const auto& unit = simulation.getUnitState(impl->self);
            path.waypoints = {unit.position, unit.position};
            return path;
        }

        // The ported emitter works in map-local coordinates, where cell (0,0)'s
        // corner is the origin; RWE's world is centred on the map. Shift by the
        // map's own corner so a route point lands where the cell really is.
        const auto origin = simulation.terrain.heightmapIndexToWorldCorner(Point(0, 0));

        path.waypoints.reserve(route.size());
        for (const auto& routePoint : route)
        {
            SimVector point(origin.x + intToSimScalar(routePoint[0]), 0_ss, origin.z + intToSimScalar(routePoint[1]));
            point.y = simulation.terrain.getHeightAt(point.x, point.z);
            path.waypoints.push_back(point);
        }
        return path;
    }

    uint32_t OaUnitPathSearch::classifyThunk(void* context, int32_t x, int32_t z)
    {
        return static_cast<Impl*>(context)->classify(x, z);
    }
}
