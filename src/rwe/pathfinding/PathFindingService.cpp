#include "PathFindingService.h"
#include <rwe/pathfinding/BugWalk.h>
#include <rwe/pathfinding/UnitPathFinder.h>
#include <rwe/pathfinding/UnitPerimeterPathFinder.h>
#include <rwe/pathfinding/pathfinding_utils.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    /**
     * A search that has been started and may not have finished.
     *
     * It is defined here rather than in the header because it holds a
     * pathfinder, and every pathfinder header reaches GameSimulation, which
     * reaches PathFindingService.
     */
    struct PathFindingService::ActiveSearch
    {
        UnitId unitId;
        /**
         * What the unit was asking for when the search started. Checked again
         * on every tick the search is resumed: a unit that has died or been
         * given a different order has no use for the answer, and finishing it
         * would write a path to a place it no longer wants to go.
         */
        PathDestination destination;
        /** The footprint the search started from, which sizes its waypoints. */
        DiscreteRect start;
        Point goal;
        std::unique_ptr<AbstractUnitPathFinder> pathFinder;
    };

    // Out of line, all four of them, because ActiveSearch is incomplete in the
    // header and a unique_ptr to it cannot be destroyed there.
    PathFindingService::PathFindingService() = default;
    PathFindingService::~PathFindingService() = default;
    PathFindingService::PathFindingService(PathFindingService&&) noexcept = default;
    PathFindingService& PathFindingService::operator=(PathFindingService&&) noexcept = default;

    void PathFindingService::abandonSearch()
    {
        activeSearch.reset();
    }

    std::optional<DiscreteRect> PathFindingService::suspendedSearchStart() const
    {
        if (!activeSearch)
        {
            return std::nullopt;
        }

        return activeSearch->start;
    }

    std::size_t PathFindingService::suspendedSearchExpansions() const
    {
        return activeSearch ? activeSearch->pathFinder->expansionsSoFar() : 0;
    }

    void PathFindingService::restoreSuspendedSearch(const GameSimulation& simulation, const DiscreteRect& start, std::size_t expansions)
    {
        if (simulation.pathRequests.empty())
        {
            return;
        }

        auto unitId = simulation.pathRequests.front().unitId;
        auto unit = simulation.tryGetUnitState(unitId);
        if (!unit)
        {
            return;
        }

        const auto* movingState = std::get_if<NavigationStateMoving>(&unit->get().navigationState.state);
        if (movingState == nullptr)
        {
            return;
        }

        beginSearch(simulation, unitId, movingState->pathDestination, start);

        // Deterministic, so running the expansions again lands on exactly the
        // state that was suspended. It cannot finish part way: it did not
        // finish in this many expansions the first time round either.
        activeSearch->pathFinder->stepSearch(static_cast<unsigned int>(expansions));
        assert(!activeSearch->pathFinder->isSearchFinished());
    }

    void PathFindingService::update(GameSimulation& simulation)
    {
        int remainingBudget = expansionBudgetPerTick;

        auto& requests = simulation.pathRequests;

        while (remainingBudget > 0)
        {
            if (activeSearch)
            {
                // A search from an earlier tick owns the scratch until it is
                // done, so it is carried on before anything new begins. It is
                // worth asking first whether anyone still wants it.
                auto unit = simulation.tryGetUnitState(activeSearch->unitId);
                auto movingState = unit
                    ? std::get_if<NavigationStateMoving>(&unit->get().navigationState.state)
                    : nullptr;
                if (movingState == nullptr || movingState->pathDestination != activeSearch->destination)
                {
                    abandonSearch();
                    ++counters.searchesAbandoned;
                    continue;
                }
            }
            else
            {
                if (requests.empty())
                {
                    break;
                }

                auto unit = simulation.tryGetUnitState(requests.front().unitId);
                if (!unit)
                {
                    // Unit that made the request no longer exists.
                    // Possibly the unit died. Just skip it.
                    requests.pop_front();
                    continue;
                }

                auto movingState = std::get_if<NavigationStateMoving>(&unit->get().navigationState.state);
                if (movingState == nullptr)
                {
                    requests.pop_front();
                    continue;
                }

                beginSearch(simulation, requests.front().unitId, movingState->pathDestination);
                ++counters.searches;
            }

            auto expansions = activeSearch->pathFinder->stepSearch(static_cast<unsigned int>(remainingBudget));
            remainingBudget -= static_cast<int>(expansions);
            counters.expansions += static_cast<long long>(expansions);

            if (!activeSearch->pathFinder->isSearchFinished())
            {
                // The tick ran out before the search did. Everything it has
                // worked out stays where it is, and next tick carries on from
                // here rather than starting again -- which is the whole point
                // of the exercise, and what the original does at 0x40EEAF.
                ++counters.searchesSuspended;
                break;
            }

            assert(!requests.empty() && requests.front().unitId == activeSearch->unitId);

            auto path = finishSearch(simulation);

            auto& unit = simulation.getUnitState(activeSearch->unitId);
            auto movingState = std::get_if<NavigationStateMoving>(&unit.navigationState.state);
            assert(movingState != nullptr);

            // If the destination itself cannot be reached, remember the closest
            // point that can be, so arrival there counts as arrival.
            movingState->reachableDestination = path.destinationUnreachable && !path.waypoints.empty()
                ? std::make_optional(path.waypoints.back())
                : std::nullopt;

            movingState->path = PathFollowingInfo(std::move(path), simulation.gameTime);
            movingState->pathRequested = false;

            activeSearch.reset();
            requests.pop_front();
        }

        counters.deferredRequests += static_cast<long long>(requests.size());
    }

    void PathFindingService::beginSearch(const GameSimulation& simulation, UnitId unitId, const PathDestination& destination, const std::optional<DiscreteRect>& startOverride)
    {
        const auto& unit = simulation.getUnitState(unitId);
        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);

        auto start = startOverride
            ? *startOverride
            : simulation.computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        auto startPoint = Point(start.x, start.y);

        auto movementClassId = match(
            unitDefinition.movementCollisionInfo, [&](const UnitDefinition::NamedMovementClass& mc) { return std::make_optional(mc.movementClassId); }, [&](const auto&) { return std::optional<MovementClassId>(); });

        std::unique_ptr<AbstractUnitPathFinder> pathFinder;
        auto goal = Point(0, 0);

        match(
            destination,
            [&](const SimVector& position) {
                auto goalRegion = simulation.computeFootprintRegion(position, unitDefinition.movementCollisionInfo);
                goal = Point(goalRegion.x, goalRegion.y);

                auto finder = std::make_unique<UnitPathFinder>(&simulation, &simulation.movementClassCollisionService, unitId, movementClassId, start.width, start.height, goal, &scratch);

                // The cheap first pass runs before the search is seeded: it
                // asks about cells outside a search on purpose, and seeding
                // stamps the scratch it would otherwise be reading.
                relaxGoalToWhatIsReachable(*finder, startPoint, goal);
                finder->beginSearch(startPoint);
                pathFinder = std::move(finder);
            },
            [&](const DiscreteRect& rect) {
                // expand the goal rect to take into account our own collision rect
                auto goalRect = rect.expandTopLeft(start.width, start.height);
                goal = Point(goalRect.x, goalRect.y);

                auto finder = std::make_unique<UnitPerimeterPathFinder>(&simulation, &simulation.movementClassCollisionService, unitId, movementClassId, start.width, start.height, goalRect, &scratch);
                finder->beginSearch(startPoint);
                pathFinder = std::move(finder);
            });

        activeSearch = std::unique_ptr<ActiveSearch>(new ActiveSearch{unitId, destination, start, goal, std::move(pathFinder)});
    }

    UnitPath PathFindingService::finishSearch(const GameSimulation& simulation)
    {
        auto& search = *activeSearch;
        auto path = search.pathFinder->takeResult();
        lastPathDebugInfo = AStarPathInfo<Point, PathCost>{path.type, path.path, std::move(path.closedVertices), path.exhausted};

        assert(path.path.size() >= 1);

        // A search is never cut short now: it stops when it reaches the goal
        // or when its open list empties. So a partial result means one thing
        // only -- there is no route at all, because every cell that could be
        // reached has been looked at -- and the unit walks to the closest
        // cell the search found, as TA does. The branch that used to aim at
        // the goal anyway was for the other kind of partial, the one the
        // thousand-expansion cap produced, and there is no longer such a cap.
        bool unreachable = path.type == AStarPathType::Partial;
        if (unreachable)
        {
            ++counters.searchesExhausted;
        }

        LOG_DEBUG << "Path for unit " << search.unitId.value << " from " << search.start.x << "," << search.start.y
                  << " to " << search.goal.x << "," << search.goal.y
                  << ": " << (unreachable ? "unreachable" : "complete") << ", " << path.path.size() << " steps";

        const auto& unit = simulation.getUnitState(search.unitId);

        if (path.path.size() == 1)
        {
            // The path is trivial, we are already at the goal
            // (or as close to it as we can get). Two points even so: the
            // follower walks the segment between the corner behind the unit
            // and the one ahead of it, so a path always has both.
            const auto* position = std::get_if<SimVector>(&search.destination);
            auto only = (unreachable || position == nullptr) ? unit.position : *position;
            return UnitPath{std::vector<SimVector>{unit.position, only}, unreachable};
        }

        auto simplifiedPath = runSimplifyPath(path.path);

        // The cell the unit started in is kept rather than dropped. It is the
        // navigator's wp[0], the corner the unit has left, and without it
        // there is no segment for the follower to project its aim point onto
        // -- see TOTALA-EXE.md section 102. The original's emitter builds it
        // the same way as every other corner (0x40E11F).
        std::vector<SimVector> waypoints;
        for (auto it = simplifiedPath.cbegin(); it != simplifiedPath.cend(); ++it)
        {
            waypoints.push_back(getWorldCenter(simulation, DiscreteRect(it->x, it->y, search.start.width, search.start.height)));
        }

        // A point destination is walked to exactly, not to the middle of the
        // cell it happens to sit in. A rect destination has no such point.
        if (!unreachable)
        {
            if (const auto* position = std::get_if<SimVector>(&search.destination))
            {
                waypoints.back() = *position;
            }
        }

        return UnitPath{std::move(waypoints), unreachable};
    }

    void PathFindingService::relaxGoalToWhatIsReachable(UnitPathFinder& pathFinder, const Point& start, const Point& goal)
    {
        // A walk is cheap and the answer is worth having: if it reaches the
        // goal there is nothing to relax, and if it does not, the search can
        // stop at anything as close as the walk managed rather than proving
        // the whole reachable map is not the goal.
        if (!relaxGoalWithFirstPass)
        {
            return;
        }

        auto result = bugWalk(
            start,
            goal,
            [&](const Point& p) { return pathFinder.isWalkableOutsideSearch(p); },
            [&](const Point& p) { return octileDistanceScore(p, goal); },
            BugWalkStepLimit);

        counters.bugWalkSteps += result.steps;

        if (result.reachedGoal)
        {
            return;
        }

        auto reachable = octileDistanceScore(result.closest, goal);
        if (reachable == 0 || reachable >= octileDistanceScore(start, goal))
        {
            // Either it is standing on the answer already, or the walk got
            // nowhere and has nothing to offer. Let the search do what it
            // did before.
            return;
        }

        pathFinder.setAcceptableDistance(reachable);
        ++counters.searchesRelaxed;
    }

    SimVector PathFindingService::getWorldCenter(const GameSimulation& simulation, const DiscreteRect& rect)
    {
        auto corner = simulation.terrain.heightmapIndexToWorldCorner(rect.x, rect.y);

        auto halfWorldWidth = (SimScalar(rect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss;
        auto halfWorldHeight = (SimScalar(rect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss;

        auto center = corner + SimVector(halfWorldWidth, 0_ss, halfWorldHeight);
        center.y = simulation.terrain.getHeightAt(center.x, center.z);
        return center;
    }
}
