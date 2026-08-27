#include "PathFindingService.h"
#include <rwe/pathfinding/UnitPathFinder.h>
#include <rwe/pathfinding/UnitPerimeterPathFinder.h>
#include <rwe/pathfinding/pathfinding_utils.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    static const unsigned int MaxTasksPerTick = 10;

    void PathFindingService::update(GameSimulation& simulation)
    {
        int remainingBudget = 4000;

        auto& requests = simulation.pathRequests;
        while (!requests.empty() && remainingBudget > 0)
        {
            auto& request = requests.front();

            auto unit = simulation.tryGetUnitState(request.unitId);
            if (!unit)
            {
                // Unit that made the request no longer exists.
                // Possibly the unit died. Just skip it.
                requests.pop_front();
                continue;
            }

            if (auto movingState = std::get_if<NavigationStateMoving>(&unit->get().navigationState.state); movingState != nullptr)
            {
                auto path = match(
                    movingState->pathDestination,
                    [&](const SimVector& pos) {
                        return findPath(simulation, request.unitId, pos);
                    },
                    [&](const DiscreteRect& pos) {
                        return findPath(simulation, request.unitId, pos);
                    });

                // If the destination itself cannot be reached, remember the closest
                // point that can be, so arrival there counts as arrival.
                movingState->reachableDestination = path.destinationUnreachable && !path.waypoints.empty()
                    ? std::make_optional(path.waypoints.back())
                    : std::nullopt;

                movingState->path = PathFollowingInfo(std::move(path), simulation.gameTime);
                movingState->pathRequested = false;

                // HACK: we know that lastPathDebugInfo is set by the call to findPath,
                // so we'll exploit it here to deduct from out budget.
                remainingBudget -= getSize(lastPathDebugInfo.closedVertices);
            }

            requests.pop_front();
        }
    }

    UnitPath PathFindingService::findPath(const GameSimulation& simulation, UnitId unitId, const DiscreteRect& destination)
    {
        const auto& unit = simulation.getUnitState(unitId);
        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);

        auto start = simulation.computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        // expand the goal rect to take into account our own collision rect
        auto goal = destination.expandTopLeft(start.width, start.height);

        auto movementClassId = match(
            unitDefinition.movementCollisionInfo, [&](const UnitDefinition::NamedMovementClass& mc) { return std::make_optional(mc.movementClassId); }, [&](const auto&) { return std::optional<MovementClassId>(); });

        UnitPerimeterPathFinder pathFinder(&simulation, &simulation.movementClassCollisionService, unitId, movementClassId, start.width, start.height, goal);

        auto path = pathFinder.findPath(Point(start.x, start.y));
        lastPathDebugInfo = AStarPathInfo<Point, PathCost>{path.type, path.path, std::move(path.closedVertices), path.exhausted};

        assert(path.path.size() >= 1);

        LOG_DEBUG << "Path for unit " << unitId.value << " from " << start.x << "," << start.y << " to rect " << goal.x << "," << goal.y << " " << goal.width << "x" << goal.height
                  << ": " << (path.type == AStarPathType::Complete ? "complete" : (path.exhausted ? "unreachable" : "partial")) << ", " << path.path.size() << " steps";

        // An exhausted search means no cell next to the footprint can be
        // reached at all (walled in, or the site is on an island).
        bool unreachable = path.type == AStarPathType::Partial && path.exhausted;

        if (path.path.size() == 1)
        {
            // The path is trivial, we are already at the goal
            // (or as close to it as we can get).
            return UnitPath{std::vector<SimVector>{unit.position}, unreachable};
        }

        auto simplifiedPath = runSimplifyPath(path.path);

        std::vector<SimVector> waypoints;
        for (auto it = ++simplifiedPath.cbegin(); it != simplifiedPath.cend(); ++it)
        {
            waypoints.push_back(getWorldCenter(simulation, DiscreteRect(it->x, it->y, start.width, start.height)));
        }

        return UnitPath{std::move(waypoints), unreachable};
    }

    UnitPath PathFindingService::findPath(const GameSimulation& simulation, UnitId unitId, const SimVector& destination)
    {
        const auto& unit = simulation.getUnitState(unitId);
        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);

        auto start = simulation.computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        auto goal = simulation.computeFootprintRegion(destination, unitDefinition.movementCollisionInfo);

        auto movementClassId = match(
            unitDefinition.movementCollisionInfo, [&](const UnitDefinition::NamedMovementClass& mc) { return std::make_optional(mc.movementClassId); }, [&](const auto&) { return std::optional<MovementClassId>(); });

        UnitPathFinder pathFinder(&simulation, &simulation.movementClassCollisionService, unitId, movementClassId, start.width, start.height, Point(goal.x, goal.y));

        auto path = pathFinder.findPath(Point(start.x, start.y));
        lastPathDebugInfo = AStarPathInfo<Point, PathCost>{path.type, path.path, std::move(path.closedVertices), path.exhausted};

        bool unreachable = false;
        if (path.type == AStarPathType::Partial)
        {
            // A partial path either means the search gave up (budget) or that
            // the goal genuinely cannot be reached: blocked, or in a region we
            // cannot get to. In the first case aim for the goal anyway and let
            // the next request carry on from wherever we get to. In the second
            // case stop at the closest point we found, as TA does.
            unreachable = path.exhausted || !pathFinder.isWalkable(Point(goal.x, goal.y));
            if (!unreachable)
            {
                path.path.emplace_back(goal.x, goal.y);
            }
        }

        assert(path.path.size() >= 1);

        if (path.path.size() == 1)
        {
            // The path is trivial, we are already at the goal
            // (or as close to it as we can get).
            return UnitPath{std::vector<SimVector>{unreachable ? unit.position : destination}, unreachable};
        }

        auto simplifiedPath = runSimplifyPath(path.path);

        std::vector<SimVector> waypoints;
        for (auto it = ++simplifiedPath.cbegin(); it != simplifiedPath.cend(); ++it)
        {
            waypoints.push_back(getWorldCenter(simulation, DiscreteRect(it->x, it->y, start.width, start.height)));
        }
        if (!unreachable)
        {
            waypoints.back() = destination;
        }

        return UnitPath{std::move(waypoints), unreachable};
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
